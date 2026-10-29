#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QVariantMap>
#include <QSet>
#include <QList>
#include <functional>

/**
 * @brief JSON 负载解析与指令分发器
 *
 * 支持两种报文：
 * 1. 信封格式（新）：{"v":1,"action":"power.restart","params":{...},"msg_id":"...","ts":...}
 * 2. 旧格式（兼容 home/esp32）：{"value":"K1"}
 */
class JsonCommandDispatcher : public QObject
{
    Q_OBJECT

public:
    // 旧式指令处理回调：入参为提取出的指令值，返回值为执行是否成功
    using HandlerFunc = std::function<bool(const QString &value)>;
    // 信封动作处理回调：入参为 params，detail 回传人类可读的执行说明
    using ActionHandlerFunc = std::function<bool(const QVariantMap &params, QString &detail)>;

    /** 信封中 ts 超过该秒数视为过期，不再执行 */
    static constexpr qint64 kMaxCommandAgeSec = 300;
    /** 保留的 msg_id 去重记录条数 */
    static constexpr int kDedupCapacity = 100;

    explicit JsonCommandDispatcher(QObject *parent = nullptr);
    ~JsonCommandDispatcher() override = default;

    /**
     * @brief 纯解析接口：从 JSON 原始负载中解析出目标键的值
     */
    bool parsePayload(const QByteArray &payload,
                      QString &outValue,
                      const QString &targetKey = "value",
                      QString *outError = nullptr);

    /**
     * @brief 执行旧式指令：根据指令字符查找并运行对应的业务逻辑
     */
    bool executeCommand(const QString &command);

    /**
     * @brief 复合接口：解析负载并按 action 或 value 调度执行
     */
    void processAndDispatch(const QString &topic, const QByteArray &payload);

    /** 注册特定指令的处理逻辑（旧式 value 分支） */
    void registerHandler(const QString &commandKey, HandlerFunc handler);

    /** 注册信封 action 的处理逻辑 */
    void registerActionHandler(const QString &action, ActionHandlerFunc handler);

    /** 移除特定指令的处理逻辑 */
    void unregisterHandler(const QString &commandKey);

    /** 设置未匹配到特定指令时的通用兜底处理逻辑 */
    void setDefaultHandler(HandlerFunc handler);
    void setShortcutHandler(HandlerFunc handler);

    /**
     * @brief 将主题加入严格模式：仅接受信封报文，不做 value/character 兜底
     *
     * 用于外部可写主题（如 home/feishu），防止伪造 value 触发 character 匹配。
     */
    void setStrictTopic(const QString &topic, bool strict = true);

    /** 查询某个主题是否处于严格模式 */
    bool isStrictTopic(const QString &topic) const;

signals:
    // 指令成功解析信号（供 UI 层或日志层监听）
    void commandExtracted(const QString &topic, const QString &command);
    // 解析或执行失败错误信号
    void dispatchError(const QString &errorMsg, const QByteArray &rawPayload);
    /**
     * @brief 信封动作的执行结果，供上层回执使用
     * @param status 取 ok / denied / expired / duplicate / failed / unknown
     */
    void actionResult(const QString &topic, const QString &action,
                      const QString &msgId, const QString &status,
                      const QString &detail);

private:
    /** 处理信封报文 */
    void dispatchEnvelope(const QString &topic, const QVariantMap &envelope);
    /** msg_id 去重：已存在返回 false，否则登记并返回 true */
    bool rememberMessageId(const QString &msgId);

    QHash<QString, HandlerFunc> m_handlers;
    QHash<QString, ActionHandlerFunc> m_actionHandlers;
    QSet<QString> m_strictTopics;
    QList<QString> m_recentMessageIds;
    HandlerFunc m_defaultHandler;
    HandlerFunc m_shortcutHandler;
};
