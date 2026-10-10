#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <functional>

/**
 * @brief JSON 负载解析与指令分发器
 */
class JsonCommandDispatcher : public QObject
{
    Q_OBJECT

public:
    // 指令处理回调：入参为提取出的指令值，返回值为执行是否成功
    using HandlerFunc = std::function<bool(const QString &value)>;

    explicit JsonCommandDispatcher(QObject *parent = nullptr);
    ~JsonCommandDispatcher() override = default;

    /**
     * @brief 纯解析接口：从 JSON 原始负载中解析出目标键的值
     * @param payload 原始 JSON 字节流
     * @param outValue 解析出的指令字符串
     * @param targetKey 目标字段名，默认为 "value"
     * @param outError 可选错误信息输出
     * @return 解析成功返回 true，否则返回 false
     */
    bool parsePayload(const QByteArray &payload,
                      QString &outValue,
                      const QString &targetKey = "value",
                      QString *outError = nullptr);

    /**
     * @brief 执行指令：根据指令字符查找并运行对应的业务逻辑
     * @param command 指令字符串
     * @return 执行成功返回 true
     */
    bool executeCommand(const QString &command);

    /**
     * @brief 复合接口：解析负载并直接调度执行
     * @param topic 消息来源主题
     * @param payload 原始数据流
     */
    void processAndDispatch(const QString &topic, const QByteArray &payload);

    /**
     * @brief 注册特定指令的处理逻辑
     * @param commandKey 指令字符串/特征字符
     * @param handler 对应执行的函数或 Lambda
     */
    void registerHandler(const QString &commandKey, HandlerFunc handler);

    /**
     * @brief 移除特定指令的处理逻辑
     */
    void unregisterHandler(const QString &commandKey);

    /**
     * @brief 设置未匹配到特定指令时的通用兜底处理逻辑
     */
    void setDefaultHandler(HandlerFunc handler);
    void setShortcutHandler(HandlerFunc handler);

signals:
    // 指令成功解析信号（供 UI 层或日志层监听）
    void commandExtracted(const QString &topic, const QString &command);
    // 解析或执行失败错误信号
    void dispatchError(const QString &errorMsg, const QByteArray &rawPayload);

private:
    QHash<QString, HandlerFunc> m_handlers;
    HandlerFunc m_defaultHandler;
    HandlerFunc m_shortcutHandler;
};
