#ifndef MQTTHANDLER_H
#define MQTTHANDLER_H

#include <QObject>
#include <QMqttClient>
#include <QSettings>
#include <QtQml/qqmlregistration.h>
#include "JsonCommandDispatcher.h"

class JsonCommandDispatcher;
class ShortcutController;
class SystemController;
class FeishuConfig;

class MqttHandler : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // 暴露配置属性给 QML 用于回显历史值
    Q_PROPERTY(QString host READ host NOTIFY configChanged)
    Q_PROPERTY(quint16 port READ port NOTIFY configChanged)
    Q_PROPERTY(QString user READ user NOTIFY configChanged)
    Q_PROPERTY(QString password READ password NOTIFY configChanged)
    // 飞书命令允许的发送者 open_id；留空表示不做校验（不建议）
    Q_PROPERTY(QString allowedSender READ allowedSender NOTIFY configChanged)

public:
    /** 设备/NFC 指令主题（旧格式 {"value":"K1"} 兼容） */
    static const QString kEsp32Topic;
    /** 飞书命令主题（信封格式，严格模式） */
    static const QString kFeishuTopic;
    /** 执行回执主题，中继订阅后转回飞书 */
    static const QString kFeishuAckTopic;
    /** 能力清单主题，retained，中继据此决定转发哪些动作 */
    static const QString kFeishuCapsTopic;
    /** 命令表主题，retained，Armbian 据此完成「文本 → action」映射 */
    static const QString kFeishuConfigTopic;

    explicit MqttHandler(QObject *parent = nullptr);

    // 属性读取函数
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QString user() const { return m_user; }
    QString password() const { return m_password; }
    QString allowedSender() const { return m_allowedSender; }

    Q_INVOKABLE void tryAutoConnect();

    // 供 QML 设置窗口调用：保存到本地并立即发起连接
    Q_INVOKABLE void saveAndConnect(const QString &host, quint16 port,
                                    const QString &user, const QString &password);

    // 供程序启动时静默自动连接（若已有保存的历史配置）
    Q_INVOKABLE void autoConnect();

    // 底层连接与订阅方法
    Q_INVOKABLE void connectToBroker(const QString &host, quint16 port = 1883,
                                     const QString &user = QString(),
                                     const QString &password = QString());
    Q_INVOKABLE void subscribeToTopic(const QString &topic);

    // 设置飞书命令允许的发送者 open_id（留空则不做校验）
    Q_INVOKABLE void setAllowedSender(const QString &openId);

    // 内部唯一的快捷键控制器，供 QML 与 MQTT 指令共用同一份状态
    ShortcutController *shortcutController() const { return m_shortcutController; }

    // 飞书命令表配置，供 QML 展示当前生效的命令
    FeishuConfig *feishuConfig() const { return m_feishuConfig; }

signals:
    void connectionSuccess();
    void connectionError(const QString &errorMsg);
    void messageReceived(const QString &topic, const QString &value);
    void configChanged();
    /** 需要界面弹出的通知（例如飞书 notify.show 动作） */
    void notifyRequested(const QString &title, const QString &body);

private slots:
    void handleMessage(const QByteArray &message, const QMqttTopicName &topic);
    void handleConnected();
    void handleDisconnected();
    void handleActionResult(const QString &topic, const QString &action,
                            const QString &msgId, const QString &status,
                            const QString &detail);

private:
    void loadSettings();
    void saveSettings(const QString &host, quint16 port, const QString &user, const QString &password);
    void processDeviceData(const QString &topicName, const QByteArray &payload);

    /** 根据配置下限决定本次动作的电源倒计时秒数 */
    int readDelay(const QVariantMap &params) const;
    /** 注册信封格式的全部 action 处理器 */
    void registerActionHandlers();
    /** 发布 retained 能力清单，供中继读取 */
    void publishCaps();
    /** 发布 retained 命令表，供 Armbian 侧映射使用 */
    void publishFeishuConfig();
    /** 校验来自飞书主题的消息是否满足发送者白名单，不通过时已发出 denied 回执 */
    bool checkSender(const QString &topic, const QByteArray &payload);
    /** PC 侧兜底映射：把 {"text": "重启"} 转成 {action, params}，用于自测与中继失效兜底 */
    bool resolveTextEnvelope(const QVariantMap &envelope, QVariantMap &outAction);
    /** 发布执行回执 */
    void publishAck(const QString &action, const QString &msgId,
                    const QString &status, const QString &detail);

    QMqttClient *m_client;
    JsonCommandDispatcher *m_dispatcher;
    ShortcutController *m_shortcutController;
    SystemController *m_systemController;
    FeishuConfig *m_feishuConfig;

    // 持久化字段缓存
    QString m_host;
    quint16 m_port = 1883;
    QString m_user;
    QString m_password;
    // 兼容旧版 QSettings 配置的发送者白名单，已迁移到 config/feishu/commands.json
    QString m_allowedSender;
};

#endif // MQTTHANDLER_H