#include "JsonCommandDispatcher.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>

JsonCommandDispatcher::JsonCommandDispatcher(QObject *parent)
    : QObject(parent)
{
}

bool JsonCommandDispatcher::parsePayload(const QByteArray &payload,
                                         QString &outValue,
                                         const QString &targetKey,
                                         QString *outError)
{
    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &jsonError);

    // 1. 校验 JSON 格式
    if (jsonError.error != QJsonParseError::NoError)
    {
        QString err = QString("JSON 解析错误: %1").arg(jsonError.errorString());
        if (outError)
            *outError = err;
        return false;
    }

    // 2. 校验顶层是否为 Object
    if (!doc.isObject())
    {
        QString err = "数据格式错误: 顶层结构非 JSON 对象";
        if (outError)
            *outError = err;
        return false;
    }

    QJsonObject jsonObj = doc.object();

    // 3. 校验关键字段是否存在
    if (!jsonObj.contains(targetKey))
    {
        QString err = QString("字段缺失: 找不到目标键 '%1'").arg(targetKey);
        if (outError)
            *outError = err;
        return false;
    }

    // 4. 类型转换提取
    QJsonValue val = jsonObj.value(targetKey);
    if (val.isString())
    {
        outValue = val.toString();
    }
    else if (val.isDouble())
    {
        outValue = QString::number(val.toDouble());
    }
    else if (val.isBool())
    {
        outValue = val.toBool() ? "true" : "false";
    }
    else if (val.isNull())
    {
        outValue = "";
    }
    else
    {
        // 针对数组或嵌套对象，输出其紧凑 JSON 文本
        if (val.isObject())
        {
            outValue = QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
        }
        else if (val.isArray())
        {
            outValue = QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact));
        }
        else
        {
            outValue = QString::fromUtf8(payload);
        }
    }

    return true;
}

bool JsonCommandDispatcher::executeCommand(const QString &command)
{
    if (command.trimmed().startsWith("K", Qt::CaseInsensitive))
    {
        if (m_shortcutHandler)
            return m_shortcutHandler(command.trimmed().toUpper());
        qWarning() << "[Dispatcher] 未设置快捷键处理器:" << command;
        return false;
    }

    // 优先匹配精确指令
    if (m_handlers.contains(command))
    {
        qDebug() << "[Dispatcher] 命中专属指令处理器:" << command;
        return m_handlers[command](command);
    }

    // 未命中特定规则时，使用默认兜底逻辑
    if (m_defaultHandler)
    {
        qDebug() << "[Dispatcher] 执行通用默认处理器，指令:" << command;
        return m_defaultHandler(command);
    }

    qWarning() << "[Dispatcher] 未找到匹配的指令处理器且未设置默认处理器:" << command;
    return false;
}

void JsonCommandDispatcher::processAndDispatch(const QString &topic, const QByteArray &payload)
{
    QJsonParseError jsonError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &jsonError);

    if (jsonError.error != QJsonParseError::NoError || !doc.isObject())
    {
        QString errorMsg = jsonError.error != QJsonParseError::NoError
                               ? QString("JSON 解析错误: %1").arg(jsonError.errorString())
                               : QString("数据格式错误: 顶层结构非 JSON 对象");
        qDebug() << "[Dispatcher 错误]" << errorMsg << "| 原始数据:" << payload;
        emit dispatchError(errorMsg, payload);
        return;
    }

    const QVariantMap envelope = doc.object().toVariantMap();

    // 信封报文以 action 判别；旧格式回落 value
    if (envelope.contains("action"))
    {
        dispatchEnvelope(topic, envelope);
        return;
    }

    if (m_strictTopics.contains(topic))
    {
        QString errorMsg = QString("严格模式主题 %1 仅接受 action 信封报文").arg(topic);
        qWarning() << "[Dispatcher]" << errorMsg << "| 原始数据:" << payload;
        emit dispatchError(errorMsg, payload);
        emit actionResult(topic, QString(), QString(), "denied", errorMsg);
        return;
    }

    QString command;
    QString errorMsg;

    if (!parsePayload(payload, command, "value", &errorMsg))
    {
        qDebug() << "[Dispatcher 错误]" << errorMsg << "| 原始数据:" << payload;
        emit dispatchError(errorMsg, payload);
        return;
    }

    emit commandExtracted(topic, command);

    bool ok = executeCommand(command);
    if (!ok)
    {
        qWarning() << "[Dispatcher] 指令执行失败:" << command;
    }
}

void JsonCommandDispatcher::dispatchEnvelope(const QString &topic, const QVariantMap &envelope)
{
    const QString action = envelope.value("action").toString().trimmed();
    const QString msgId = envelope.value("msg_id").toString().trimmed();
    const QVariantMap params = envelope.value("params").toMap();

    emit commandExtracted(topic, action);

    // 1. 协议版本
    if (envelope.contains("v") && envelope.value("v").toInt() != 1)
    {
        const QString detail = QString("不支持的协议版本: %1").arg(envelope.value("v").toInt());
        qWarning() << "[Dispatcher]" << detail;
        emit actionResult(topic, action, msgId, "denied", detail);
        return;
    }

    // 2. 空 action
    if (action.isEmpty())
    {
        const QString detail = "信封缺少 action 字段";
        qWarning() << "[Dispatcher]" << detail;
        emit actionResult(topic, action, msgId, "denied", detail);
        return;
    }

    // 3. 过期判定：避免 PC 离线期间 Broker 排队的命令在重连后突然执行
    const qint64 ts = envelope.value("ts").toLongLong();
    if (ts > 0)
    {
        const qint64 now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
        const qint64 age = now - ts;
        if (age > kMaxCommandAgeSec)
        {
            const QString detail = QString("命令已过期 %1 秒，超过上限 %2 秒")
                                       .arg(age)
                                       .arg(kMaxCommandAgeSec);
            qWarning() << "[Dispatcher]" << detail << "| action:" << action;
            emit actionResult(topic, action, msgId, "expired", detail);
            return;
        }
    }

    // 4. msg_id 去重：QoS 1 重复投递与飞书侧重试都会造成重复执行
    if (!msgId.isEmpty() && !rememberMessageId(msgId))
    {
        const QString detail = QString("msg_id 重复，已忽略: %1").arg(msgId);
        qInfo() << "[Dispatcher]" << detail;
        emit actionResult(topic, action, msgId, "duplicate", detail);
        return;
    }

    // 5. 查表执行
    if (!m_actionHandlers.contains(action))
    {
        const QString detail = QString("未注册的动作: %1").arg(action);
        qWarning() << "[Dispatcher]" << detail << "| 主题:" << topic;
        emit actionResult(topic, action, msgId, "unknown", detail);
        return;
    }

    QString detail;
    bool ok = false;
    try
    {
        ok = m_actionHandlers.value(action)(params, detail);
    }
    catch (const std::exception &e)
    {
        detail = QString::fromUtf8("处理器异常: ") + e.what();
        ok = false;
    }

    if (detail.isEmpty())
        detail = ok ? QStringLiteral("执行成功") : QStringLiteral("执行失败");

    if (ok)
    {
        qInfo() << "[Dispatcher] 动作执行成功:" << action << "|" << detail;
        emit actionResult(topic, action, msgId, "ok", detail);
    }
    else
    {
        qWarning() << "[Dispatcher] 动作执行失败:" << action << "|" << detail;
        emit actionResult(topic, action, msgId, "failed", detail);
    }
}

bool JsonCommandDispatcher::rememberMessageId(const QString &msgId)
{
    if (m_recentMessageIds.contains(msgId))
        return false;

    m_recentMessageIds.append(msgId);
    while (m_recentMessageIds.size() > kDedupCapacity)
        m_recentMessageIds.removeFirst();
    return true;
}

void JsonCommandDispatcher::registerHandler(const QString &commandKey, HandlerFunc handler)
{
    m_handlers.insert(commandKey, handler);
}

void JsonCommandDispatcher::registerActionHandler(const QString &action, ActionHandlerFunc handler)
{
    m_actionHandlers.insert(action, std::move(handler));
}

void JsonCommandDispatcher::unregisterHandler(const QString &commandKey)
{
    m_handlers.remove(commandKey);
}

void JsonCommandDispatcher::setDefaultHandler(HandlerFunc handler)
{
    m_defaultHandler = std::move(handler);
}

void JsonCommandDispatcher::setShortcutHandler(HandlerFunc handler)
{
    m_shortcutHandler = std::move(handler);
}

void JsonCommandDispatcher::setStrictTopic(const QString &topic, bool strict)
{
    if (strict)
        m_strictTopics.insert(topic);
    else
        m_strictTopics.remove(topic);
}

bool JsonCommandDispatcher::isStrictTopic(const QString &topic) const
{
    return m_strictTopics.contains(topic);
}
