#include "FeishuConfig.h"
#include "ConfigPaths.h"
#include "FeatureMap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QSaveFile>
#include <QRegularExpression>

namespace
{
const char *kSupportedMatchTypes[] = {"exact", "prefix", "regex"};
}

FeishuConfig::FeishuConfig(QObject *parent)
    : QObject(parent), m_watcher(nullptr)
{
    m_featureMap = new FeatureMap(this);
}

QString FeishuConfig::configPath() const
{
    return ConfigPaths::feishuFilePath(QStringLiteral("commands.json"));
}

QSet<QString> FeishuConfig::knownActions()
{
    // 动作白名单由代码决定，不从配置文件读取：
    // 配置写错或被人改坏，也只能影响「命令词到已存在动作的映射」，不能凭空执行新能力。
    return QSet<QString>{
        QStringLiteral("power.restart"),
        QStringLiteral("power.shutdown"),
        QStringLiteral("power.cancel"),
        QStringLiteral("power.lock"),
        QStringLiteral("power.sleep"),
        QStringLiteral("app.launch"),
        QStringLiteral("shortcut.run"),
        QStringLiteral("media.control"),
        QStringLiteral("clipboard.set"),
        QStringLiteral("notify.show"),
    };
}

bool FeishuConfig::writeDefaultConfig()
{
    if (!ConfigPaths::ensureFeishuDir())
    {
        qWarning() << "[FeishuConfig] 无法创建目录:" << ConfigPaths::feishuDir();
        return false;
    }

    auto makeCommand = [](const QString &id, const QStringList &patterns,
                          const QString &action, const QJsonObject &params,
                          const QString &reply, bool enabled = true)
    {
        QJsonObject match;
        match["type"] = "exact";
        match["patterns"] = QJsonArray::fromStringList(patterns);

        QJsonObject cmd;
        cmd["id"] = id;
        cmd["enabled"] = enabled;
        cmd["match"] = match;
        cmd["action"] = action;
        cmd["params"] = params;
        cmd["reply"] = reply;
        return cmd;
    };

    QJsonObject restartParams;
    restartParams["delay"] = kDefaultPowerDelaySec;

    QJsonArray commands;
    commands.append(makeCommand(QStringLiteral("restart"),
                                {QStringLiteral("重启"), QStringLiteral("重启电脑")},
                                QStringLiteral("power.restart"), restartParams,
                                QStringLiteral("已计划 60 秒后重启，发送「取消关机」可撤销")));
    commands.append(makeCommand(QStringLiteral("shutdown"),
                                {QStringLiteral("关机"), QStringLiteral("关闭电脑")},
                                QStringLiteral("power.shutdown"), restartParams,
                                QStringLiteral("已计划 60 秒后关机，发送「取消关机」可撤销")));
    commands.append(makeCommand(QStringLiteral("cancel"),
                                {QStringLiteral("取消关机"), QStringLiteral("取消重启")},
                                QStringLiteral("power.cancel"), QJsonObject(),
                                QStringLiteral("已取消待执行的关机/重启")));
    commands.append(makeCommand(QStringLiteral("lock"),
                                {QStringLiteral("锁屏")},
                                QStringLiteral("power.lock"), QJsonObject(),
                                QStringLiteral("已锁定工作站")));
    commands.append(makeCommand(QStringLiteral("sleep"),
                                {QStringLiteral("睡眠")},
                                QStringLiteral("power.sleep"), QJsonObject(),
                                QStringLiteral("已进入睡眠")));
    commands.append(makeCommand(QStringLiteral("mute"),
                                {QStringLiteral("静音")},
                                QStringLiteral("media.control"),
                                QJsonObject{{"op", QStringLiteral("mute")}},
                                QStringLiteral("已执行静音")));

    // 前缀匹配：把「启动 N1」里 N1 之外的文本作为 character 取出来
    QJsonObject launchMatch;
    launchMatch["type"] = "prefix";
    launchMatch["pattern"] = QStringLiteral("启动 ");
    launchMatch["captureAs"] = QStringLiteral("character");
    QJsonObject launchCmd;
    launchCmd["id"] = QStringLiteral("launch");
    launchCmd["enabled"] = true;
    launchCmd["match"] = launchMatch;
    launchCmd["action"] = QStringLiteral("app.launch");
    launchCmd["params"] = QJsonObject();
    launchCmd["reply"] = QStringLiteral("正在启动 {character}");
    commands.append(launchCmd);

    // 指令 → 功能 映射表：armbian 映射出的指令名在这里决定执行什么。
    // 新增功能优先改这里，脚本类示例见 scripts 节点（path 相对 config 目录）。
    QJsonObject features;
    features["restart"] = QJsonObject{{"type", "builtin"}, {"target", "power.restart"},
                                       {"params", QJsonObject{{"delay", kDefaultPowerDelaySec}}}};
    features["shutdown"] = QJsonObject{{"type", "builtin"}, {"target", "power.shutdown"},
                                        {"params", QJsonObject{{"delay", kDefaultPowerDelaySec}}}};
    features["cancel"] = QJsonObject{{"type", "builtin"}, {"target", "power.cancel"}};
    features["lock"] = QJsonObject{{"type", "builtin"}, {"target", "power.lock"}};
    features["sleep"] = QJsonObject{{"type", "builtin"}, {"target", "power.sleep"}};
    features["mute"] = QJsonObject{{"type", "builtin"}, {"target", "media.control"},
                                    {"params", QJsonObject{{"op", "mute"}}}};
    features["launch"] = QJsonObject{{"type", "builtin"}, {"target", "app.launch"},
                                      {"captureAs", "character"}};
    features["notify"] = QJsonObject{{"type", "builtin"}, {"target", "notify.show"}};

    QJsonObject root;
    root["schema"] = 1;
    root["minPowerDelay"] = kDefaultPowerDelaySec;
    root["allowedSenders"] = QJsonArray();
    root["unknownReply"] = QStringLiteral("未识别的命令：{raw}");
    root["commands"] = commands;
    root["features"] = features;

    QSaveFile file(configPath());
    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning() << "[FeishuConfig] 无法写入默认配置:" << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        qWarning() << "[FeishuConfig] 默认配置提交失败:" << file.errorString();
        return false;
    }
    qInfo() << "[FeishuConfig] 已生成默认命令表:" << configPath();
    return true;
}

bool FeishuConfig::parseCommand(const QJsonObject &obj, int index, QVariantMap &out, QString &error)
{
    const QString id = obj.value("id").toString().trimmed();
    if (id.isEmpty())
    {
        error = QStringLiteral("第 %1 条命令缺少 id").arg(index + 1);
        return false;
    }

    const QString action = obj.value("action").toString().trimmed();
    if (action.isEmpty())
    {
        error = QStringLiteral("命令 %1 缺少 action").arg(id);
        return false;
    }
    if (!knownActions().contains(action))
    {
        error = QStringLiteral("命令 %1 使用了未知动作: %2").arg(id, action);
        return false;
    }

    const QJsonObject match = obj.value("match").toObject();
    const QString matchType = match.value("type").toString().trimmed();
    bool typeOk = false;
    for (const char *t : kSupportedMatchTypes)
        if (matchType == QLatin1String(t))
            typeOk = true;
    if (!typeOk)
    {
        error = QStringLiteral("命令 %1 的 match.type 无效: %2").arg(id, matchType);
        return false;
    }

    QVariantMap entry;
    entry["id"] = id;
    entry["enabled"] = obj.value("enabled").toBool(true);
    entry["action"] = action;
    entry["params"] = obj.value("params").toObject().toVariantMap();
    entry["reply"] = obj.value("reply").toString();
    entry["matchType"] = matchType;

    if (matchType == QLatin1String("prefix"))
    {
        const QString pattern = match.value("pattern").toString();
        if (pattern.isEmpty())
        {
            error = QStringLiteral("命令 %1 的 match.pattern 为空").arg(id);
            return false;
        }
        entry["pattern"] = pattern;
        entry["captureAs"] = match.value("captureAs").toString();
    }
    else if (matchType == QLatin1String("regex"))
    {
        const QString pattern = match.value("pattern").toString();
        if (pattern.isEmpty())
        {
            error = QStringLiteral("命令 %1 的 match.pattern 为空").arg(id);
            return false;
        }
        entry["pattern"] = pattern;
    }
    else
    {
        QStringList patterns;
        for (const QJsonValue &v : match.value("patterns").toArray())
        {
            const QString p = v.toString().trimmed();
            if (!p.isEmpty())
                patterns.append(p);
        }
        if (patterns.isEmpty())
        {
            error = QStringLiteral("命令 %1 的 match.patterns 为空").arg(id);
            return false;
        }
        entry["patterns"] = patterns;
    }

    out = entry;
    return true;
}

bool FeishuConfig::load()
{
    m_lastErrors.clear();
    m_commands.clear();
    m_allowedSenders.clear();

    const QString path = configPath();
    if (!QFileInfo::exists(path))
    {
        if (!writeDefaultConfig())
        {
            m_lastErrors << QStringLiteral("无法生成默认配置: %1").arg(path);
            emit configReloaded();
            return false;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_lastErrors << QStringLiteral("无法读取配置: %1").arg(file.errorString());
        emit configReloaded();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError)
    {
        // 配置写坏时保留上一次的有效配置，只记录错误，避免整机失去命令能力
        m_lastErrors << QStringLiteral("JSON 解析失败: %1").arg(parseError.errorString());
        qWarning() << "[FeishuConfig] JSON 解析失败，沿用上一次有效配置:" << parseError.errorString();
        emit configReloaded();
        return false;
    }
    if (!doc.isObject())
    {
        m_lastErrors << QStringLiteral("配置顶层必须是 JSON 对象");
        emit configReloaded();
        return false;
    }

    const QJsonObject root = doc.object();

    const int schema = root.value("schema").toInt(1);
    if (schema != 1)
    {
        qWarning() << "[FeishuConfig] 未知的 schema 版本:" << schema << "，按 schema 1 解析";
    }

    // 电源倒计时：配置只能上抬，不能低于硬下限
    int delay = root.value("minPowerDelay").toInt(kDefaultPowerDelaySec);
    if (delay < kHardMinPowerDelaySec)
    {
        qWarning() << "[FeishuConfig] minPowerDelay" << delay << "低于硬下限"
                   << kHardMinPowerDelaySec << "，已按硬下限取值";
        delay = kHardMinPowerDelaySec;
    }
    m_minPowerDelay = delay;

    for (const QJsonValue &v : root.value("allowedSenders").toArray())
    {
        const QString sender = v.toString().trimmed();
        if (!sender.isEmpty())
            m_allowedSenders.append(sender);
    }

    m_unknownReply = root.value("unknownReply").toString();
    if (m_unknownReply.isEmpty())
        m_unknownReply = QStringLiteral("未识别的命令：{raw}");

    QSet<QString> seenIds;
    const QJsonArray commands = root.value("commands").toArray();
    for (int i = 0; i < commands.size(); ++i)
    {
        if (!commands.at(i).isObject())
        {
            m_lastErrors << QStringLiteral("第 %1 条命令不是对象，已跳过").arg(i + 1);
            continue;
        }

        QVariantMap entry;
        QString error;
        if (!parseCommand(commands.at(i).toObject(), i, entry, error))
        {
            m_lastErrors << error;
            qWarning() << "[FeishuConfig]" << error << "（已跳过该条）";
            continue;
        }

        const QString id = entry.value("id").toString();
        if (seenIds.contains(id))
        {
            m_lastErrors << QStringLiteral("命令 id 重复: %1（已跳过）").arg(id);
            continue;
        }
        seenIds.insert(id);

        if (!entry.value("enabled").toBool())
            continue;

        m_commands.append(entry);
    }

    if (m_commands.isEmpty())
        m_lastErrors << QStringLiteral("没有任何生效的命令条目");

    // 指令 → 功能 映射表：配置中的 features 覆盖内置默认值
    QStringList featureErrors;
    m_featureMap->load(root, featureErrors);
    for (const QString &e : featureErrors)
        m_lastErrors << e;

    // 热重载：监听文件变化，保存后自动重新加载
    if (!m_watcher)
    {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged,
                this, [this](const QString &changed)
                {
        qInfo() << "[FeishuConfig] 检测到配置变化，重新加载:" << changed;
        load();
        // 部分编辑器保存时会替换文件，导致监听丢失，这里重新挂载
        if (!m_watcher->files().contains(configPath()) && QFileInfo::exists(configPath()))
            m_watcher->addPath(configPath()); });
    }
    if (m_watcher->files().contains(path))
        m_watcher->removePath(path);
    if (QFileInfo::exists(path))
        m_watcher->addPath(path);

    qInfo() << "[FeishuConfig] 已加载" << m_commands.size() << "条命令，minPowerDelay ="
            << m_minPowerDelay << "，允许发送者" << m_allowedSenders.size() << "个";
    for (const QString &e : m_lastErrors)
        qWarning() << "[FeishuConfig]" << e;

    emit configReloaded();
    return true;
}

QString FeishuConfig::normalizeText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text)
    {
        // 全角字符（U+FF01..U+FF5E）转半角，其余原样保留：
        // 飞书输入法容易产出全角空格与标点，不归一化会导致同一条命令匹配不上。
        if (ch.unicode() >= 0xFF01 && ch.unicode() <= 0xFF5E)
            out.append(QChar(ch.unicode() - 0xFEE0));
        else if (ch.unicode() == 0x3000)
            out.append(QChar(' '));
        else
            out.append(ch);
    }
    out = out.simplified();
    for (int i = 0; i < out.size(); ++i)
        if (out.at(i).unicode() < 0x80)
            out[i] = out.at(i).toLower();
    return out;
}

QVariantMap FeishuConfig::resolveText(const QString &text) const
{
    const QString normalized = normalizeText(text);
    if (normalized.isEmpty())
        return {};

    QVariantMap result;
    // 按配置顺序匹配，首个命中即停止：优先级由数组顺序表达，不需要额外的 priority 字段
    for (const QVariant &v : m_commands)
    {
        const QVariantMap cmd = v.toMap();
        const QString matchType = cmd.value("matchType").toString();
        QVariantMap params = cmd.value("params").toMap();

        if (matchType == QStringLiteral("exact"))
        {
            const QStringList patterns = cmd.value("patterns").toStringList();
            for (const QString &rawPattern : patterns)
            {
                if (normalizeText(rawPattern) == normalized)
                    return result = QVariantMap{{"action", cmd.value("action")},
                                                {"params", params},
                                                {"id", cmd.value("id")}};
            }
        }
        else if (matchType == QStringLiteral("prefix"))
        {
            const QString pattern = normalizeText(cmd.value("pattern").toString());
            if (pattern.isEmpty() || !normalized.startsWith(pattern))
                continue;

            const QString captured = normalized.mid(pattern.size()).trimmed();
            if (captured.isEmpty())
                continue;

            const QString captureAs = cmd.value("captureAs").toString();
            if (!captureAs.isEmpty())
                params[captureAs] = captured;
            return result = QVariantMap{{"action", cmd.value("action")},
                                        {"params", params},
                                        {"id", cmd.value("id")}};
        }
        else if (matchType == QStringLiteral("regex"))
        {
            QRegularExpression re(cmd.value("pattern").toString(),
                                  QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid())
                continue;
            const QRegularExpressionMatch m = re.match(normalized);
            if (!m.hasMatch())
                continue;

            // 命名捕获组直接并入 params，便于表达「启动 N1」这类带参数的命令
            const QStringList names = re.namedCaptureGroups();
            for (int i = 1; i < names.size(); ++i)
            {
                if (!names.at(i).isEmpty() && m.captured(i).size() > 0)
                    params[names.at(i)] = m.captured(i);
            }
            return result = QVariantMap{{"action", cmd.value("action")},
                                        {"params", params},
                                        {"id", cmd.value("id")}};
        }
    }
    return {};
}

QVariantList FeishuConfig::commandSummary() const
{
    QVariantList summary;
    for (const QVariant &v : m_commands)
    {
        const QVariantMap cmd = v.toMap();
        QVariantMap item;
        item["id"] = cmd.value("id");
        item["action"] = cmd.value("action");
        item["matchType"] = cmd.value("matchType");
        summary.append(item);
    }
    return summary;
}

QJsonObject FeishuConfig::toPublishJson() const
{
    QJsonObject root;
    root["schema"] = 1;
    root["minPowerDelay"] = m_minPowerDelay;
    root["unknownReply"] = m_unknownReply;

    QJsonArray senders;
    for (const QString &s : m_allowedSenders)
        senders.append(s);
    root["allowedSenders"] = senders;

    QJsonArray commands;
    for (const QVariant &v : m_commands)
    {
        const QVariantMap cmd = v.toMap();
        QJsonObject obj;
        obj["id"] = cmd.value("id").toString();
        obj["action"] = cmd.value("action").toString();
        obj["params"] = QJsonObject::fromVariantMap(cmd.value("params").toMap());
        obj["reply"] = cmd.value("reply").toString();

        QJsonObject match;
        const QString matchType = cmd.value("matchType").toString();
        match["type"] = matchType;
        if (matchType == QStringLiteral("exact"))
            match["patterns"] = QJsonArray::fromStringList(cmd.value("patterns").toStringList());
        else
            match["pattern"] = cmd.value("pattern").toString();
        if (matchType == QStringLiteral("prefix"))
            match["captureAs"] = cmd.value("captureAs").toString();

        obj["match"] = match;
        commands.append(obj);
    }
    root["commands"] = commands;
    return root;
}