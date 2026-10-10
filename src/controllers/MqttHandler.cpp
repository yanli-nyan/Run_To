#include "MqttHandler.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include "ConfigLauncher.h"
#include "ShortcutController.h"


MqttHandler::MqttHandler(QObject *parent) : QObject(parent)
{
    m_client = new QMqttClient(this);
    m_dispatcher = new JsonCommandDispatcher(this);
    auto *shortcutController = new ShortcutController(m_dispatcher);

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
    m_dispatcher->setShortcutHandler([shortcutController](const QString &cmd)
                                     { return shortcutController->executeShortcut(cmd); });

    // 2. 注册特定字符/指令的独立分支逻辑示例
    m_dispatcher->registerHandler("REBOOT", [](const QString &val)
                                  {
            qDebug() << "执行系统重启逻辑...";
            return true; });

    m_dispatcher->registerHandler("VOLUME_MUTE", [](const QString &val)
                                  {
            qDebug() << "执行静音操作...";
            return true; });

    // 3. 关联信号
    connect(m_dispatcher, &JsonCommandDispatcher::commandExtracted,
            this, &MqttHandler::messageReceived);

    // 实例构造时自动从本地读取已保存的配置
    loadSettings();
}

void MqttHandler::loadSettings()
{
    QSettings settings;
    settings.beginGroup("MQTT");
    m_host = settings.value("host", "").toString();
    m_port = static_cast<quint16>(settings.value("port", 1883).toUInt());
    m_user = settings.value("user", "").toString();
    m_password = settings.value("password", "").toString();
    settings.endGroup();

    qDebug() << "已加载 MQTT 配置，Host:" << (m_host.isEmpty() ? "(未配置)" : m_host);
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
    settings.endGroup();
    settings.sync(); // 立即刷入磁盘

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

    // 自动订阅目标主题
    subscribeToTopic("home/desktop/nfc_command");
}

void MqttHandler::handleDisconnected()
{
    qDebug() << "MQTT 已断开连接";
}

void MqttHandler::handleMessage(const QByteArray &message, const QMqttTopicName &topic)
{
    qDebug() << "[后台接收] 主题:" << topic.name() << "长度:" << message.size() << "bytes";
    processDeviceData(topic.name(), message);
}

void MqttHandler::processDeviceData(const QString &topicName, const QByteArray &payload)
{
    m_dispatcher->processAndDispatch(topicName, payload);
}

void MqttHandler::tryAutoConnect()
{
    // 获取当前缓存的 Host（根据实际成员变量名调整，如 m_host）
    QString host = m_host.trimmed();

    if (!host.isEmpty())
    {
        qDebug() << "[启动自连] 检测到配置缓存，开始自动连接 Broker:" << host;
        // 调用现有连接接口
        saveAndConnect(host, 1883, m_user, m_password);
    }
    else
    {
        qDebug() << "[启动自连] 本地未配置 MQTT 地址，跳过自动连接";
    }
}
