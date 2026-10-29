#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QStringList>
#include <QJsonObject>
#include <QtQml/qqmlregistration.h>
#include <functional>

/**
 * @brief 指令 → 功能 映射表
 *
 * 分工：中文到指令的映射由 Armbian 完成，PC 只负责把拿到的指令名转成实际动作。
 * 这张表把「指令名」和「执行方式」解耦，后续接入脚本或外部程序只需在配置里加条目。
 *
 * 配置位置：config/feishu/commands.json 的 "features" 字段（见 FeishuConfig）。
 * 未在配置中出现的指令沿用内置默认表。
 *
 * 条目结构：
 *   { "type": "builtin" | "script" | "action",
 *     "target": "power.restart" | "scripts/deploy.ps1" | "notify.show",
 *     "params": { ... },
 *     "confirm": false }
 *
 * type 语义：
 *   builtin  交给内置动作白名单处理，target 为动作名（如 power.restart）
 *   script   运行外部脚本/程序，target 为相对 config 目录的路径，或绝对路径
 *   action   与 builtin 等价，保留用于兼容 armbian 直接下发 action 的报文
 */
class FeatureMap : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    /** 内置动作执行器；具体动作由 MqttHandler 承担，本类不重复实现 */
    using BuiltinRunner = std::function<bool(const QString &action, const QVariantMap &params, QString &detail)>;

    explicit FeatureMap(QObject *parent = nullptr);
    ~FeatureMap() override = default;

    /** 注入内置动作执行器（MqttHandler 构造时设置） */
    void setBuiltinRunner(BuiltinRunner runner) { m_builtinRunner = std::move(runner); }

    /** 加载完成的功能条目数量 */
    int count() const { return m_features.size(); }

    /**
     * @brief 从配置对象中加载功能表；未配置的指令保留内置默认值
     * @param root config/feishu/commands.json 的顶层对象
     * @param errors 收集条目级错误，不影响其他条目
     */
    void load(const QJsonObject &root, QStringList &errors);

    /** 查询某指令是否有可用功能 */
    Q_INVOKABLE bool contains(const QString &command) const { return m_features.contains(command); }

    /** 列出全部已注册的指令名 */
    Q_INVOKABLE QStringList commands() const { return m_features.keys(); }

    /**
     * @brief 执行某指令对应的功能
     * @param command 指令名
     * @param outParams 实际生效的参数（配置默认值合并入参后）
     * @param detail 人类可读的执行结果说明
     * @return 是否执行成功
     */
    bool execute(const QString &command, const QVariantMap &params,
                 QVariantMap &outParams, QString &detail);

    /** 供界面/日志展示的精简摘要 */
    Q_INVOKABLE QVariantList summary() const;

    /** 脚本类功能允许的扩展名白名单 */
    static QStringList scriptExtensions();

signals:
    void changed();

private:
    /** 执行外部脚本 */
    bool runScript(const QVariantMap &feature, const QVariantMap &params,
                   QVariantMap &outParams, QString &detail);
    /** 把脚本路径解析为绝对路径（相对路径以 config 目录为基准） */
    QString resolveScriptPath(const QString &target) const;

    QHash<QString, QVariantMap> m_features;
    BuiltinRunner m_builtinRunner;

};
