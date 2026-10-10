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
#ifdef Q_OS_WIN
#include <windows.h>
#endif
ShortcutController::ShortcutController(QObject *p) : QObject(p) {}
QString ShortcutController::filePath() const { return QDir(QCoreApplication::applicationDirPath()).filePath("key.json"); }
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
        const QStringList parts = param.split(QRegularExpression(QStringLiteral("\\s*\\+\\s*")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) return false;
        QList<WORD> keys;
        for (const QString &part : parts) {
            const QString key = part.trimmed().toUpper();
            WORD virtualKey = 0;
            if (key == QStringLiteral("CTRL")) virtualKey = VK_CONTROL;
            else if (key == QStringLiteral("ALT")) virtualKey = VK_MENU;
            else if (key == QStringLiteral("SHIFT")) virtualKey = VK_SHIFT;
            else if (key == QStringLiteral("WIN")) virtualKey = VK_LWIN;
            else if (key.size() == 1) virtualKey = static_cast<WORD>(VkKeyScanW(key.at(0).unicode()) & 0xff);
            if (!virtualKey) return false;
            keys.append(virtualKey);
        }
        QList<INPUT> inputs;
        for (WORD key : keys) {
            INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; inputs.append(input);
        }
        for (auto it = keys.crbegin(); it != keys.crend(); ++it) {
            INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = *it; input.ki.dwFlags = KEYEVENTF_KEYUP; inputs.append(input);
        }
        return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == static_cast<UINT>(inputs.size());
    }
#endif

    qWarning() << "[ShortcutController] 不支持的快捷键动作:" << actionType;
    return false;
}
