#include "ShortcutController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QGuiApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QDebug>
#include "AppLauncher.h"
#include "ConfigPaths.h"
#include <QHash>
#ifdef Q_OS_WIN
#include <windows.h>
#include <algorithm>
#endif

namespace
{
// 全角字符转半角：中文输入法下很容易打出"Ｗｉｎ　＋　Ｓｐａｃｅ"这类全角组合。
inline QChar toHalfWidth(QChar c)
{
    const ushort u = c.unicode();
    if (u >= 0xFF01 && u <= 0xFF5E) return QChar(static_cast<ushort>(u - 0xFEE0));
    if (u == 0x3000) return QChar(u' ');
    if (u == 0xFF0B) return QChar(u'+');
    return c;
}
}

// 把 "Ctrl + Space" / "Ctrl+Ｓｐａｃｅ" 拆成大写按键名。全角的 ＋ 与空格都按半角处理。
QStringList splitComboKeys(const QString &param)
{
    QString normalized;
    normalized.reserve(param.size());
    for (const QChar c : param) normalized.append(toHalfWidth(c));
    return normalized.split(QRegularExpression(QStringLiteral("\\s*\\+\\s*")), Qt::SkipEmptyParts);
}

#ifdef Q_OS_WIN
// 按键名 → 虚拟键码。特别地，空格这类不可见键必须用键名 "Space" 表达，
// 因为按 "+" 切分后单独的空白字符会被 SkipEmptyParts 丢掉。
WORD virtualKeyForName(const QString &name)
{
    const QString key = name.trimmed().toUpper();
    if (key.isEmpty()) return 0;

    static const QHash<QString, WORD> named{
        {QStringLiteral("CTRL"), VK_CONTROL},   {QStringLiteral("CONTROL"), VK_CONTROL},
        {QStringLiteral("ALT"), VK_MENU},       {QStringLiteral("SHIFT"), VK_SHIFT},
        {QStringLiteral("WIN"), VK_LWIN},       {QStringLiteral("LWIN"), VK_LWIN},
        {QStringLiteral("RWIN"), VK_RWIN},      {QStringLiteral("SPACE"), VK_SPACE},
        {QStringLiteral("TAB"), VK_TAB},        {QStringLiteral("ENTER"), VK_RETURN},
        {QStringLiteral("RETURN"), VK_RETURN},  {QStringLiteral("ESC"), VK_ESCAPE},
        {QStringLiteral("ESCAPE"), VK_ESCAPE},  {QStringLiteral("BACKSPACE"), VK_BACK},
        {QStringLiteral("BKSP"), VK_BACK},      {QStringLiteral("DELETE"), VK_DELETE},
        {QStringLiteral("DEL"), VK_DELETE},     {QStringLiteral("INSERT"), VK_INSERT},
        {QStringLiteral("INS"), VK_INSERT},     {QStringLiteral("UP"), VK_UP},
        {QStringLiteral("DOWN"), VK_DOWN},      {QStringLiteral("LEFT"), VK_LEFT},
        {QStringLiteral("RIGHT"), VK_RIGHT},    {QStringLiteral("HOME"), VK_HOME},
        {QStringLiteral("END"), VK_END},        {QStringLiteral("PAGEUP"), VK_PRIOR},
        {QStringLiteral("PGUP"), VK_PRIOR},     {QStringLiteral("PAGEDOWN"), VK_NEXT},
        {QStringLiteral("PGDN"), VK_NEXT},      {QStringLiteral("PRINTSCREEN"), VK_SNAPSHOT},
    };
    if (const auto it = named.constFind(key); it != named.cend()) return it.value();

    // F1 ~ F24
    if (key.startsWith(u'F') && key.size() <= 3) {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24) return static_cast<WORD>(VK_F1 + n - 1);
    }

    // 其余按单个可见字符处理（C、V、1 等）
    if (key.size() == 1) return static_cast<WORD>(VkKeyScanW(key.at(0).unicode()) & 0xff);
    return 0;
}
#endif

ShortcutController::ShortcutController(QObject *p) : QObject(p) {}
// key.json 统一存放在 exe 上一级目录的 config 文件夹中
QString ShortcutController::filePath() const
{
    ConfigPaths::ensureDir();
    ConfigPaths::migrateLegacyFile(QStringLiteral("key.json"));
    return ConfigPaths::filePath(QStringLiteral("key.json"));
}
QVariantList ShortcutController::defaultShortcuts() const
{
    QVariantList r;
    QStringList n = {"按键 1", "按键 2", "按键 3", "按键 4", "按键 5", "按键 6"}, t = {"组合快捷键", "组合快捷键", "启动应用", "多媒体控制", "发送文本", "组合快捷键"}, p = {"Ctrl + C", "Ctrl + V", "notepad.exe", "静音 / 取消静音", "Thank you!", "Win + D"}, i = {"📋", "📄", "🚀", "🔇", "💬", "🖥️"};
    for (int x = 0; x < 6; ++x)
    {
        QVariantMap v;
        v["keyId"] = x + 1;
        v["keyName"] = n[x];
        v["actionType"] = t[x];
        v["param"] = p[x];
        v["icon"] = i[x];
        r.append(v);
    }
    return r;
}
QVariantList ShortcutController::loadShortcuts()
{
    QFile f(filePath());
    if (!f.exists())
        return defaultShortcuts();
    if (!f.open(QIODevice::ReadOnly))
    {
        emit errorOccurred("无法读取快捷键配置：" + f.errorString());
        return defaultShortcuts();
    }
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject())
    {
        emit errorOccurred("快捷键配置格式无效：" + e.errorString());
        return defaultShortcuts();
    }
    auto a = d.object()["keys"].toArray();
    return a.isEmpty() ? defaultShortcuts() : a.toVariantList();
}
bool ShortcutController::saveShortcuts(const QVariantList &s)
{
    QJsonObject o;
    o["version"] = "1.0";
    o["keys"] = QJsonArray::fromVariantList(s);
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly))
    {
        emit errorOccurred("无法保存快捷键配置：" + f.errorString());
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool ShortcutController::executeShortcut(const QString &keyCode)
{
    const QString normalizedCode = keyCode.trimmed().toUpper();
    static const QRegularExpression codePattern(QStringLiteral("^K([1-9][0-9]*)$"));
    const QRegularExpressionMatch match = codePattern.match(normalizedCode);
    if (!match.hasMatch()) {
        qWarning() << "[ShortcutController] 无效的快捷键编号:" << keyCode;
        return false;
    }

    const int keyId = match.captured(1).toInt();
    const QVariantList shortcuts = loadShortcuts();
    QVariantMap selected;
    for (const QVariant &value : shortcuts) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("keyId")).toInt() == keyId) {
            selected = item;
            break;
        }
    }
    if (selected.isEmpty()) {
        qWarning() << "[ShortcutController] 未找到按键配置:" << normalizedCode;
        return false;
    }

    const QString actionType = selected.value(QStringLiteral("actionType")).toString();
    const QString param = selected.value(QStringLiteral("param")).toString();
    if (actionType == QStringLiteral("启动应用"))
        return AppLauncher::launch(param);

    if (actionType == QStringLiteral("发送文本")) {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(param);
            return true;
        }
        return false;
    }

#ifdef Q_OS_WIN
    if (actionType == QStringLiteral("多媒体控制")) {
        const WORD key = (param.contains(QStringLiteral("静音"), Qt::CaseInsensitive) ||
                          param.contains(QStringLiteral("mute"), Qt::CaseInsensitive))
                             ? VK_VOLUME_MUTE : VK_MEDIA_PLAY_PAUSE;
        INPUT input[2]{};
        input[0].type = input[1].type = INPUT_KEYBOARD;
        input[0].ki.wVk = input[1].ki.wVk = key;
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        return SendInput(2, input, sizeof(INPUT)) == 2;
    }
    if (actionType == QStringLiteral("组合快捷键")) {
        const QStringList parts = splitComboKeys(param);
        if (parts.isEmpty()) return false;
        QList<WORD> keys;
        for (const QString &part : parts) {
            const WORD virtualKey = virtualKeyForName(part);
            if (!virtualKey) {
                qWarning() << "[ShortcutController] 无法识别的按键名:" << part << "（完整参数:" << param << "）";
                return false;
            }
            keys.append(virtualKey);
        }

        // 单独一个 Win 键：整串一次发送时按下/抬起会在同一瞬间被合并，等于没按。
        // 因此只有单键 Win 组合才逐事件发送并留出间隔，多键组合仍整串发送。
        const bool hasWin = std::any_of(keys.cbegin(), keys.cend(), [](WORD k) {
            return k == VK_LWIN || k == VK_RWIN;
        });
        const DWORD gapMs = hasWin ? (keys.size() > 1 ? 5 : 30) : 0;

        QList<INPUT> inputs;
        for (WORD key : keys) {
            INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; inputs.append(input);
        }
        for (auto it = keys.crbegin(); it != keys.crend(); ++it) {
            INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = *it; input.ki.dwFlags = KEYEVENTF_KEYUP; inputs.append(input);
        }

        if (!hasWin)
            return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == static_cast<UINT>(inputs.size());

        if (gapMs > 0) {
            for (INPUT input : inputs) {
                if (SendInput(1, &input, sizeof(INPUT)) != 1) return false;
                Sleep(gapMs);
            }
            return true;
        }
        // 多键 Win 组合：整串一次发送，保证同一次焦点切换内按下与抬起成对。
        return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == static_cast<UINT>(inputs.size());
    }
#endif

    qWarning() << "[ShortcutController] 不支持的快捷键动作:" << actionType;
    return false;
}
