#include "JsonCommandDispatcher.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
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

void JsonCommandDispatcher::registerHandler(const QString &commandKey, HandlerFunc handler)
{
    m_handlers.insert(commandKey, handler);
}

void JsonCommandDispatcher::unregisterHandler(const QString &commandKey)
{
    m_handlers.remove(commandKey);
}

void JsonCommandDispatcher::setDefaultHandler(HandlerFunc handler)
{
    m_defaultHandler = handler;
}