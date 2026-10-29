#include "MqttHandler.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QMqttTopicName>
#include "ConfigLauncher.h"
#include "ShortcutController.h"
#include "SystemController.h"
#include "FeishuConfig.h"
#include "FeatureMap.h"

const QString MqttHandler::kEsp32Topic = QStringLiteral("home/esp32");
const QString MqttHandler::kFeishuTopic = QStringLiteral("home/feishu");
const QString MqttHandler::kFeishuAckTopic = QStringLiteral("home/feishu/ack");
const QString MqttHandler::kFeishuCapsTopic = QStringLiteral("home/feishu/caps");
const QString MqttHandler::kFeishuConfigTopic = QStringLiteral("home/feishu/config");

namespace
{
// 命令倒计时下限由 config/feishu/commands.json 的 minPowerDelay 决定，
// 但 PC 侧始终保一个硬下限（见 FeishuConfig::kHardMinPowerDelaySec）：
// 配置只能把倒计时调长，不能调到更短使撤销窗口失效。
} // namespace

MqttHandler::MqttHandler(QObject *parent)
    : m_client(nullptr), m_dispatcher(nullptr),
      m_shortcutController(nullptr), m_systemController(nullptr),
      m_feishuConfig(nullptr), QObject(parent)
{
    m_client = new QMqttClient(this);
    m_dispatcher = new JsonCommandDispatcher(this);
    m_shortcutController = new ShortcutController(this);
    m_systemController = new SystemController(this);
    m_feishuConfig = new FeishuConfig(this);

    // 指令 → 功能映射表：内置动作交回本类执行，本类不重复实现动作逻辑
    m_feishuConfig->featureMap()->setBuiltinRunner(
        [this](const QString &action, const QVariantMap &params, QString &detail)
        {
            if (!m_actionExecutor)
            {
                detail = QStringLiteral("动作执行器未就绪");
                return false;
            }
            return m_actionExecutor(action, params, detail);
        });

    // 命令表加载：缺失时生成默认表，写坏时沿用上一次有效配置
    m_feishuConfig->load();
    connect(m_feishuConfig, &FeishuConfig::configReloaded, this, [this]()
            {
        // 配置热重载后指令表可能变化，重新登记到分发器
        registerFeatureCommands();
        if (m_client && m_client->state() == QMqttClient::Connected)
        {
            publishFeishuConfig();
        } });

    // home/feishu 面向外部输入，只接受信封报文，禁止 value/character 兜底匹配
    m_dispatcher->setStrictTopic(kFeishuTopic, true);

    connect(m_client, &QMqttClient::connected, this, &MqttHandler::handleConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MqttHandler::handleDisconnected);
    connect(m_client, &QMqttClient::messageReceived, this, &MqttHandler::handleMessage);

    connect(m_client, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error)
            {
        QString desc;
        switch (error) {
        case QMqttClient::NoError:
            return;
        case QMqttClient::InvalidProtocolVersion:
            desc = "协议版本不支持"; break;
        case QMqttClient::IdRejected:
            desc = "Client ID 被拒绝"; break;
        case QMqttClient::ServerUnavailable:
            desc = "服务器不可用（Broker 拒绝连接）"; break;
        case QMqttClient::BadUsernameOrPassword:
            desc = "用户名或密码错误"; break;
        case QMqttClient::NotAuthorized:
            desc = "未授权（HA Mosquitto 必须填写正确的用户名密码）"; break;
        case QMqttClient::TransportInvalid:
            desc = "传输层连接失败(256)：IP 不通/端口不对/防火墙拦截，请用 Test-NetConnection <IP> -Port 1883 验证"; break;
        case QMqttClient::ProtocolViolation:
            desc = "协议违规"; break;
        case QMqttClient::Mqtt5SpecificError:
            desc = "MQTT5 协议错误"; break;
        default:
            desc = "未知错误"; break;
        }
        qDebug() << "MQTT 发生错误，错误码:" << error << "=>" << desc;
        emit connectionError(desc); });

    m_dispatcher->setDefaultHandler([](const QString &cmd)
                                    { return ConfigLauncher::launchByCharacter(cmd); });
    m_dispatcher->setShortcutHandler([this](const QString &cmd)
                                     { return m_shortcutController->executeShortcut(cmd); });

    // 旧式指令分支（home/esp32）：保留原有语义，仅把空实现的处理器转发到实际能力
    m_dispatcher->registerHandler("REBOOT", [this](const QString &)
                                  {
            QString detail;
            return m_systemController->restart(m_feishuConfig->minPowerDelay(), detail); });

    m_dispatcher->registerHandler("VOLUME_MUTE", [this](const QString &)
                                  {
            QString detail;
            return m_systemController->mediaControl(QStringLiteral("mute"), detail); });

    // 信封动作分支（home/feishu）
    registerActionHandlers();
    registerFeatureCommands();

    // 3. 关联信号
    connect(m_dispatcher, &JsonCommandDispatcher::commandExtracted,
            this, &MqttHandler::messageReceived);
    connect(m_dispatcher, &JsonCommandDispatcher::actionResult,
            this, &MqttHandler::handleActionResult);

    // 实例构造时自动从本地读取已保存的配置
    loadSettings();
}

int MqttHandler::readDelay(const QVariantMap &params) const
{
    bool ok = false;
    int delay = params.value("delay").toInt(&ok);
    const int floor = m_feishuConfig ? m_feishuConfig->minPowerDelay() : FeishuConfig::kDefaultPowerDelaySec;
    if (!ok || delay < floor)
        delay = floor;
    return delay;
}

void MqttHandler::registerActionHandlers()
{
    // 动作实现集中在一张表里：
    // 1) 逐条注册到分发器，供 home/feishu 的 action 信封直接使用；
    // 2) 同一个表也作为 FeatureMap 的内置执行器，避免两套实现漂移。
    QHash<QString, JsonCommandDispatcher::ActionHandlerFunc> actions;

    // ---- 电源类：不可逆动作，统一带倒计时与配置下限 ----
    actions[QStringLiteral("power.restart")] =
        [this](const QVariantMap &params, QString &detail)
    { return m_systemController->restart(readDelay(params), detail); };

    actions[QStringLiteral("power.shutdown")] =
        [this](const QVariantMap &params, QString &detail)
    { return m_systemController->shutdown(readDelay(params), detail); };

    actions[QStringLiteral("power.cancel")] =
        [this](const QVariantMap &, QString &detail)
    { return m_systemController->cancelShutdown(detail); };

    actions[QStringLiteral("power.lock")] =
        [this](const QVariantMap &, QString &detail)
    { return m_systemController->lockWorkstation(detail); };

    actions[QStringLiteral("power.sleep")] =
        [this](const QVariantMap &, QString &detail)
    { return m_systemController->sleep(detail); };

    // ---- 应用与快捷键：只接受本地标识，路径永不来自报文 ----
    actions[QStringLiteral("app.launch")] =
        [](const QVariantMap &params, QString &detail)
    {
        const QString character = params.value("character").toString().trimmed();
        if (character.isEmpty())
        {
            detail = QStringLiteral("params.character 为空");
            return false;
        }
        const bool ok = ConfigLauncher::launchByCharacter(character);
        detail = ok ? QStringLiteral("已启动 character=%1 对应的程序").arg(character)
                    : QStringLiteral("未找到 character=%1 对应的可启动程序").arg(character);
        return ok;
    };

    actions[QStringLiteral("shortcut.run")] =
        [this](const QVariantMap &params, QString &detail)
    {
        const QString key = params.value("key").toString().trimmed();
        if (key.isEmpty())
        {
            detail = QStringLiteral("params.key 为空");
            return false;
        }
        const bool ok = m_shortcutController->executeShortcut(key);
        detail = ok ? QStringLiteral("已执行快捷键 %1").arg(key)
                    : QStringLiteral("快捷键 %1 执行失败（检查 key.json 中是否存在该编号）").arg(key);
        return ok;
    };

    // ---- 与本机交互类 ----
    actions[QStringLiteral("media.control")] =
        [this](const QVariantMap &params, QString &detail)
    {
        const QString op = params.value("op").toString().trimmed();
        if (op.isEmpty())
        {
            detail = QStringLiteral("params.op 为空");
            return false;
        }
        return m_systemController->mediaControl(op, detail);
    };

    actions[QStringLiteral("clipboard.set")] =
        [this](const QVariantMap &params, QString &detail)
    {
        const QString text = params.value("text").toString();
        if (text.isEmpty())
        {
            detail = QStringLiteral("params.text 为空");
            return false;
        }
        return m_systemController->setClipboardText(text, detail);
    };

    actions[QStringLiteral("notify.show")] =
        [this](const QVariantMap &params, QString &detail)
    {
        const QString text = params.value("text").toString();
        if (text.isEmpty())
        {
            detail = QStringLiteral("params.text 为空");
            return false;
        }
        const QString level = params.contains("level")
                                  ? params.value("level").toString()
                                  : QStringLiteral("info");
        emit notifyRequested(level == QStringLiteral("warn") ? QStringLiteral("飞书提醒（警告）")
                                                             : QStringLiteral("飞书提醒"),
                             text);
        detail = QStringLiteral("已弹出通知");
        return true;
    };

    // ---- 未知命令：只记录，不执行 ----
    actions[QStringLiteral("unknown")] =
        [](const QVariantMap &params, QString &detail)
    {
        const QString raw = params.value("raw").toString();
        detail = QStringLiteral("未识别命令，未执行: %1").arg(raw);
        qWarning() << "[飞书] 未识别命令:" << raw;
        return false;
    };

    for (auto it = actions.constBegin(); it != actions.constEnd(); ++it)
        m_dispatcher->registerActionHandler(it.key(), it.value());

    m_actionExecutor = [actions](const QString &action, const QVariantMap &params, QString &detail)
    {
        if (!actions.contains(action))
        {
            detail = QStringLiteral("未注册的动作: %1").arg(action);
            return false;
        }
        return actions.value(action)(params, detail);
    };
}

void MqttHandler::loadSettings()
{
    QSettings settings;
    settings.beginGroup("MQTT");
    m_host = settings.value("host", "").toString();
    m_port = static_cast<quint16>(settings.value("port", 1883).toUInt());
    m_user = settings.value("user", "").toString();
    m_password = settings.value("password", "").toString();
    m_allowedSender = settings.value("allowedSender", "").toString();
    settings.endGroup();

    qDebug() << "已加载 MQTT 配置，Host:" << (m_host.isEmpty() ? "(未配置)" : m_host);
    if (m_allowedSender.isEmpty())
    {
        qDebug() << "未配置飞书发送者 open_id，home/feishu 将接受任意来源的命令";
    }
}

void MqttHandler::saveSettings(const QString &host, quint16 port,
                               const QString &user, const QString &password)
{
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password;

    QSettings settings;
    settings.beginGroup("MQTT");
    settings.setValue("host", m_host);
    settings.setValue("port", m_port);
    settings.setValue("user", m_user);
    settings.setValue("password", m_password);
    settings.setValue("allowedSender", m_allowedSender);
    settings.endGroup();
    settings.sync(); // 立即刷入磁盘

    emit configChanged();
}

void MqttHandler::setAllowedSender(const QString &openId)
{
    m_allowedSender = openId.trimmed();
    QSettings settings;
    settings.beginGroup("MQTT");
    settings.setValue("allowedSender", m_allowedSender);
    settings.endGroup();
    settings.sync();
    qDebug() << "飞书发送者白名单已更新:" << (m_allowedSender.isEmpty() ? "(空，不校验)" : m_allowedSender);
    emit configChanged();
}

void MqttHandler::saveAndConnect(const QString &host, quint16 port,
                                 const QString &user, const QString &password)
{
    saveSettings(host, port, user, password);
    connectToBroker(m_host, m_port, m_user, m_password);
}

void MqttHandler::autoConnect()
{
    if (m_host.trimmed().isEmpty())
    {
        qDebug() << "未配置 MQTT 服务器 IP，跳过自动连接";
        return;
    }
    qDebug() << "使用持久化配置发起自动连接...";
    connectToBroker(m_host, m_port, m_user, m_password);
}

void MqttHandler::connectToBroker(const QString &host, quint16 port,
                                  const QString &user, const QString &password)
{
    if (m_client->state() == QMqttClient::Connected || m_client->state() == QMqttClient::Connecting)
    {
        m_client->disconnectFromHost();
    }
    m_client->setHostname(host);
    m_client->setPort(port);
    m_client->setUsername(user);
    m_client->setPassword(password);
    qDebug() << "正在连接 MQTT Broker:" << host << ":" << port
             << (user.isEmpty() ? "(匿名)" : "(用户: " + user + ")");
    m_client->connectToHost();
}

void MqttHandler::subscribeToTopic(const QString &topic)
{
    if (m_client->state() == QMqttClient::Connected)
    {
        m_client->subscribe(QMqttTopicFilter(topic), 1);
        qDebug() << "请求订阅主题:" << topic;
    }
}

void MqttHandler::handleConnected()
{
    qDebug() << "MQTT 服务器连接成功！";
    emit connectionSuccess();

    // 命令主题：精确订阅，不要用 home/feishu/#，
    // 否则会把本机自己发布的 ack 也收回来形成回环。
    subscribeToTopic(kEsp32Topic);
    subscribeToTopic(kFeishuTopic);

    // 能力清单与命令表都用 retained 发布，中继重连后能立刻读到
    publishCaps();
    publishFeishuConfig();
}

void MqttHandler::publishCaps()
{
    QJsonObject caps;
    caps["v"] = 1;
    QJsonArray actions{
        QStringLiteral("power.restart"), QStringLiteral("power.shutdown"),
        QStringLiteral("power.cancel"), QStringLiteral("power.lock"),
        QStringLiteral("power.sleep"), QStringLiteral("app.launch"),
        QStringLiteral("shortcut.run"), QStringLiteral("media.control"),
        QStringLiteral("clipboard.set"), QStringLiteral("notify.show")};
    caps["actions"] = actions;
    caps["min_power_delay"] = m_feishuConfig ? m_feishuConfig->minPowerDelay()
                                             : FeishuConfig::kDefaultPowerDelaySec;
    caps["config_topic"] = kFeishuConfigTopic;
    caps["commands"] = m_feishuConfig ? static_cast<int>(m_feishuConfig->commandCount()) : 0;

    const QByteArray payload = QJsonDocument(caps).toJson(QJsonDocument::Compact);
    if (m_client->state() == QMqttClient::Connected)
    {
        m_client->publish(QMqttTopicName(kFeishuCapsTopic), payload, 1, true);
        qDebug() << "已发布能力清单至" << kFeishuCapsTopic;
    }
}

void MqttHandler::publishFeishuConfig()
{
    if (!m_feishuConfig || m_client->state() != QMqttClient::Connected)
        return;

    const QByteArray payload = QJsonDocument(m_feishuConfig->toPublishJson())
                                   .toJson(QJsonDocument::Compact);
    m_client->publish(QMqttTopicName(kFeishuConfigTopic), payload, 1, true);
    qInfo() << "已发布飞书命令表至" << kFeishuConfigTopic << "，条目数:"
            << m_feishuConfig->commandCount();
}

void MqttHandler::handleDisconnected()
{
    qDebug() << "MQTT 已断开连接";
}

void MqttHandler::handleMessage(const QByteArray &message, const QMqttTopicName &topic)
{
    qDebug() << "[后台接收] 主题:" << topic.name() << "长度:" << message.size() << "bytes";

    if (!checkSender(topic.name(), message))
        return;

    // 兜底入口：飞书主题收到 {"text": "重启"} 时，由 PC 用同一份命令表自行映射。
    // 正常链路是 Armbian 完成映射后发 action 信封，这里只是自测与中继失效时的容错。
    if (topic.name() == kFeishuTopic)
    {
        const QJsonObject obj = QJsonDocument::fromJson(message).object();
        if (!obj.contains("action") && obj.contains("text"))
        {
            QVariantMap out;
            if (!resolveTextEnvelope(obj.toVariantMap(), out))
                return; // 已发出 unknown 回执

            QJsonObject rewritten;
            rewritten["v"] = 1;
            rewritten["action"] = out.value("action").toString();
            rewritten["params"] = QJsonObject::fromVariantMap(out.value("params").toMap());
            rewritten["msg_id"] = obj.value("msg_id").toString();
            rewritten["ts"] = obj.value("ts").toDouble(0);
            rewritten["from"] = obj.value("from").toString();
            processDeviceData(topic.name(),
                              QJsonDocument(rewritten).toJson(QJsonDocument::Compact));
            return;
        }
    }

    processDeviceData(topic.name(), message);
}

bool MqttHandler::resolveTextEnvelope(const QVariantMap &envelope, QVariantMap &outAction)
{
    const QString raw = envelope.value("text").toString();
    const QString msgId = envelope.value("msg_id").toString();

    if (!m_feishuConfig || m_feishuConfig->commandCount() == 0)
    {
        const QString detail = QStringLiteral("命令表为空，无法映射文本: %1").arg(raw);
        qWarning() << "[飞书]" << detail;
        publishAck(QString(), msgId, QStringLiteral("unknown"), detail);
        return false;
    }

    const QVariantMap result = m_feishuConfig->resolveText(raw);
    if (!result.contains("action"))
    {
        const QString detail = m_feishuConfig->unknownReply().replace("{raw}", raw);
        qInfo() << "[飞书] 未识别命令:" << raw;
        publishAck(QString(), msgId, QStringLiteral("unknown"), detail);
        return false;
    }

    outAction = result;
    return true;
}

bool MqttHandler::checkSender(const QString &topic, const QByteArray &payload)
{
    // 只对飞书主题做发送者校验；home/esp32 由设备直发，没有 from 字段
    if (topic != kFeishuTopic)
        return true;

    // 白名单以 config/feishu/commands.json 为准；为空表示未启用校验
    const QStringList allowed = m_feishuConfig ? m_feishuConfig->allowedSenders() : QStringList();
    if (allowed.isEmpty())
        return true;

    const QJsonObject obj = QJsonDocument::fromJson(payload).object();
    const QString from = obj.value("from").toString().trimmed();
    if (allowed.contains(from))
        return true;

    const QString detail = QString("发送者不在白名单: %1").arg(from.isEmpty() ? QStringLiteral("(缺失 from)") : from);
    qWarning() << "[飞书]" << detail;
    publishAck(obj.value("action").toString(), obj.value("msg_id").toString(),
               QStringLiteral("denied"), detail);
    return false;
}

void MqttHandler::handleActionResult(const QString &topic, const QString &action,
                                     const QString &msgId, const QString &status,
                                     const QString &detail)
{
    Q_UNUSED(topic);
    publishAck(action, msgId, status, detail);
}

void MqttHandler::publishAck(const QString &action, const QString &msgId,
                             const QString &status, const QString &detail)
{
    if (m_client->state() != QMqttClient::Connected)
        return;

    QJsonObject ack;
    ack["v"] = 1;
    ack["msg_id"] = msgId;
    ack["action"] = action;
    ack["status"] = status;
    ack["detail"] = detail;
    ack["ts"] = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();

    const QByteArray payload = QJsonDocument(ack).toJson(QJsonDocument::Compact);
    // 回执保留 10 秒即可，避免长期霸占 retained
    m_client->publish(QMqttTopicName(kFeishuAckTopic), payload, 1, true);
    qDebug() << "[回执]" << status << "|" << action << "|" << detail;
}

void MqttHandler::processDeviceData(const QString &topicName, const QByteArray &payload)
{
    m_dispatcher->processAndDispatch(topicName, payload);
}

void MqttHandler::tryAutoConnect()
{
    QString host = m_host.trimmed();

    if (!host.isEmpty())
    {
        qDebug() << "[启动自连] 检测到配置缓存，开始自动连接 Broker:" << host;
        connectToBroker(host, m_port, m_user, m_password);
    }
    else
    {
        qDebug() << "[启动自连] 本地未配置 MQTT 地址，跳过自动连接";
    }
}
bool MqttHandler::executeFeature(const QString &command, const QVariantMap &params, QString &detail)
{
    if (!m_feishuConfig || !m_feishuConfig->featureMap())
    {
        detail = QStringLiteral("功能表未初始化");
        return false;
    }

    QVariantMap effective;
    const bool ok = m_feishuConfig->featureMap()->execute(command, params, effective, detail);
    if (ok && detail.isEmpty())
        detail = QStringLiteral("指令 %1 执行成功").arg(command);
    return ok;
}

void MqttHandler::registerFeatureCommands()
{
    if (!m_feishuConfig || !m_feishuConfig->featureMap())
        return;

    const QStringList commands = m_feishuConfig->featureMap()->commands();
    for (const QString &command : commands)
    {
        // 指令名走旧式 value 分支：Armbian 可以直接下发 {"value": "restart"}，
        // 与 home/esp32 的语义一致，便于中继简单拼报文。
        m_dispatcher->registerHandler(command, [this, command](const QString &)
                                      {
            QString detail;
            const bool ok = executeFeature(command, QVariantMap(), detail);
            if (ok)
                qInfo() << "[功能表]" << command << "|" << detail;
            else
                qWarning() << "[功能表]" << command << "|" << detail;
            return ok; });
    }
    qInfo() << "[功能表] 已登记" << commands.size() << "条指令到分发器";
}