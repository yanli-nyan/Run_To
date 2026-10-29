#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QJsonObject>
#include <QSet>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <functional>

/**
 * @brief 飞书命令表配置
 *
 * 配置位置：config/feishu/commands.json（见 ConfigPaths.h）
 * PC 是该配置的唯一来源：负责解析、校验、热重载，并把整份配置通过 retained MQTT
 * 主题 home/feishu/config 下发给 Armbian，由 Armbian 侧完成「文本 → action」映射。
 */
class FeishuConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(int commandCount READ commandCount NOTIFY configReloaded)
    Q_PROPERTY(int minPowerDelay READ minPowerDelay NOTIFY configReloaded)
    Q_PROPERTY(QStringList lastErrors READ lastErrors NOTIFY configReloaded)

public:
    /** PC 侧硬下限：配置只能往上调，不能调到更小值使倒计时失效 */
    static constexpr int kHardMinPowerDelaySec = 30;
    /** 配置默认值：未配置 minPowerDelay 时使用 */
    static constexpr int kDefaultPowerDelaySec = 60;

    explicit FeishuConfig(QObject *parent = nullptr);
    ~FeishuConfig() override = default;

    /** 配置文件路径（config/feishu/commands.json） */
    QString configPath() const;
    /** 有效命令条目数量 */
    int commandCount() const { return m_commands.size(); }
    /** 生效的电源倒计时下限（已应用硬下限） */
    int minPowerDelay() const { return m_minPowerDelay; }
    /** 最近一次加载产生的错误/告警，供界面与日志展示 */
    QStringList lastErrors() const { return m_lastErrors; }

    /** 每条已生效命令的 id → action，供顶层展示与排查 */
    Q_INVOKABLE QVariantList commandSummary() const;

    /** 加载并校验配置；文件缺失时写入默认配置 */
    bool load();

    /** 允许的动作名白名单（来自内置动作表，不由配置文件决定） */
    static QSet<QString> knownActions();
    /** 当前配置中用到的允许发送者列表 */
    QStringList allowedSenders() const { return m_allowedSenders; }
    /** 未识别命令时的回显模板 */
    QString unknownReply() const { return m_unknownReply; }

    /**
     * @brief 按命令表把一段文本映射为 action + params
     *
     * 供 PC 侧兜底入口自测使用；Armbian 侧按 home/feishu/config 下发的同一份
     * 命令表实现等价逻辑。未命中时返回空 map。
     */
    QVariantMap resolveText(const QString &text) const;

    /** 文本归一化：去首尾空白、合并连续空白、全角转半角、ASCII 小写 */
    static QString normalizeText(const QString &text);

    /**
     * @brief 将整份配置序列化为下发格式（不含本地私有字段）
     */
    QJsonObject toPublishJson() const;

signals:
    void configReloaded();

private:
    /** 写入一份可直接使用的默认配置（仅在文件缺失时调用） */
    bool writeDefaultConfig();
    /** 解析一条命令条目，失败返回 false 并通过 error 输出原因 */
    bool parseCommand(const QJsonObject &obj, int index, QVariantMap &out, QString &error);

    QVariantList m_commands;
    QStringList m_allowedSenders;
    QString m_unknownReply;
    int m_minPowerDelay = kDefaultPowerDelaySec;
    QStringList m_lastErrors;

    QFileSystemWatcher *m_watcher;
};