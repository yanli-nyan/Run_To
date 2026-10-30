#include "FeatureMap.h"
#include "ConfigPaths.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

namespace
{
// 脚本类功能允许的扩展名。刻意不放开 .dll/.msi 之类，
// 避免配置写错或被人改坏后变成任意文件执行入口。
const QStringList kScriptExtensions = {
    QStringLiteral("ps1"), QStringLiteral("bat"),
    QStringLiteral("cmd"), QStringLiteral("exe"),
};

// 未在配置中显式声明的指令，使用这份内置默认表。
// 改这里等于改默认行为；要让用户可调，请写进 config/feishu/commands.json 的 features。
QVariantMap builtinFeature(const QString &target, const QVariantMap &params = {})
{
    return QVariantMap{
        {"type", QStringLiteral("builtin")},
        {"target", target},
        {"params", params},
    };
}
}

FeatureMap::FeatureMap(QObject *parent) : QObject(parent)
{
    QStringList ignored;
    load(QJsonObject(), ignored);
}

QStringList FeatureMap::scriptExtensions()
{
    return kScriptExtensions;
}

void FeatureMap::load(const QJsonObject &root, QStringList &errors)
{
    m_features.clear();

    // 1. 内置默认表：覆盖现有全部动作，保证配置文件缺失时功能可用
    m_features.insert(QStringLiteral("restart"), builtinFeature(QStringLiteral("power.restart")));
    m_features.insert(QStringLiteral("shutdown"), builtinFeature(QStringLiteral("power.shutdown")));
    m_features.insert(QStringLiteral("cancel"), builtinFeature(QStringLiteral("power.cancel")));
    m_features.insert(QStringLiteral("lock"), builtinFeature(QStringLiteral("power.lock")));
    m_features.insert(QStringLiteral("sleep"), builtinFeature(QStringLiteral("power.sleep")));
    m_features.insert(QStringLiteral("mute"), builtinFeature(QStringLiteral("media.control"),
                                                            {{"op", QStringLiteral("mute")}}));
    m_features.insert(QStringLiteral("launch"), QVariantMap{
                                                  {"type", QStringLiteral("builtin")},
                                                  {"target", QStringLiteral("app.launch")},
                                                  {"params", QVariantMap{}},
                                                  {"captureAs", QStringLiteral("character")},
                                              });
    m_features.insert(QStringLiteral("notify"), builtinFeature(QStringLiteral("notify.show")));

    // 2. 配置覆盖：只覆盖同名的指令，其余保留内置默认值
    const QJsonObject features = root.value("features").toObject();
    for (auto it = features.constBegin(); it != features.constEnd(); ++it)
    {
        if (!it.value().isObject())
        {
            errors << QStringLiteral("feature %1 不是对象，已跳过").arg(it.key());
            continue;
        }

        const QJsonObject obj = it.value().toObject();
        const QString type = obj.value("type").toString().trimmed();
        if (type != QStringLiteral("builtin") && type != QStringLiteral("script") &&
            type != QStringLiteral("action"))
        {
            errors << QStringLiteral("feature %1 的 type 无效: %2").arg(it.key(), type);
            continue;
        }

        const QString target = obj.value("target").toString().trimmed();
        if (target.isEmpty())
        {
            errors << QStringLiteral("feature %1 缺少 target").arg(it.key());
            continue;
        }

        QVariantMap entry;
        entry["type"] = type;
        entry["target"] = target;
        entry["params"] = obj.value("params").toObject().toVariantMap();
        entry["confirm"] = obj.value("confirm").toBool(false);

        // 脚本类：扩展名白名单校验，路径不存在时只告警不拒绝，
        // 因为脚本可能稍后才放到文件中，过早拒绝会让排查变得困难。
        if (type == QStringLiteral("script"))
        {
            const QString suffix = QFileInfo(target).suffix().toLower();
            if (!kScriptExtensions.contains(suffix))
            {
                errors << QStringLiteral("feature %1 的脚本扩展名不受支持: .%2（允许: %3）")
                              .arg(it.key(), suffix, kScriptExtensions.join(QLatin1Char('/')));
                continue;
            }
            entry["captureAs"] = obj.value("captureAs").toString();
        }

        m_features.insert(it.key(), entry);
    }

    emit changed();
    qInfo() << "[FeatureMap] 已注册" << m_features.size() << "条指令 → 功能映射";
}

bool FeatureMap::execute(const QString &command, const QVariantMap &params,
                         QVariantMap &outParams, QString &detail)
{
    if (!m_features.contains(command))
    {
        detail = QStringLiteral("未注册的指令: %1").arg(command);
        return false;
    }

    const QVariantMap feature = m_features.value(command);
    const QString type = feature.value("type").toString();
    const QString target = feature.value("target").toString();

    // 配置里的 params 作为默认值，报文带来的 params 覆盖其上
    QVariantMap merged = feature.value("params").toMap();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        merged.insert(it.key(), it.value());
    outParams = merged;

    if (type == QStringLiteral("script"))
        return runScript(feature, merged, outParams, detail);

    if (!m_builtinRunner)
    {
        detail = QStringLiteral("内置动作执行器未注入");
        return false;
    }
    return m_builtinRunner(target, merged, detail);
}

bool FeatureMap::runScript(const QVariantMap &feature, const QVariantMap &params,
                           QVariantMap &outParams, QString &detail)
{
    // 脚本执行不产生额外输出参数：合并后的 params 已由 execute() 写入 outParams，
    // 这里只是保持签名一致，显式声明为该参数在本函数内不使用。
    Q_UNUSED(outParams);

    const QString target = feature.value("target").toString();
    const QString path = resolveScriptPath(target);

    QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        detail = QStringLiteral("脚本不存在: %1").arg(path);
        return false;
    }

    const QString suffix = info.suffix().toLower();

    // 参数以 name=value 形式逐个追加，不经过 shell 拼接，
    // 避免命令词里的引号或空格被解释成额外的命令。
    QStringList args;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        args << QStringLiteral("--%1=%2").arg(it.key(), it.value().toString());

    QString program;
    QStringList fullArgs;
    if (suffix == QStringLiteral("ps1"))
    {
        // 优先用 pwsh，回退到 Windows PowerShell
        program = QStringLiteral("powershell.exe");
        fullArgs << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
                 << QStringLiteral("Bypass") << QStringLiteral("-File") << path << args;
    }
    else if (suffix == QStringLiteral("bat") || suffix == QStringLiteral("cmd"))
    {
        program = QStringLiteral("cmd.exe");
        fullArgs << QStringLiteral("/c") << path << args;
    }
    else
    {
        program = path;
        fullArgs = args;
    }

    QProcess proc;
    proc.setWorkingDirectory(info.absolutePath());
    proc.start(program, fullArgs);
    if (!proc.waitForStarted(3000))
    {
        detail = QStringLiteral("脚本启动失败: %1").arg(proc.errorString());
        return false;
    }
    if (!proc.waitForFinished(30000))
    {
        proc.kill();
        detail = QStringLiteral("脚本执行超时（30 秒）: %1").arg(path);
        return false;
    }

    const int code = proc.exitCode();
    if (code != 0)
    {
        const QString err = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        detail = QStringLiteral("脚本退出码 %1%2").arg(code).arg(
            err.isEmpty() ? QString() : QStringLiteral("，输出: ") + err.left(200));
        return false;
    }

    detail = QStringLiteral("脚本执行成功: %1").arg(QFileInfo(target).fileName());
    return true;
}

QString FeatureMap::resolveScriptPath(const QString &target) const
{
    QFileInfo info(target);
    if (info.isAbsolute())
        return QDir::cleanPath(target);
    // 相对路径以 config 目录为基准（config/feishu/scripts/... 也写在相对路径里）
    return QDir::cleanPath(QDir(ConfigPaths::configDir()).filePath(target));
}

QVariantList FeatureMap::summary() const
{
    QVariantList list;
    for (auto it = m_features.constBegin(); it != m_features.constEnd(); ++it)
    {
        const QVariantMap f = it.value();
        QVariantMap item;
        item["command"] = it.key();
        item["type"] = f.value("type");
        item["target"] = f.value("target");
        list.append(item);
    }
    return list;
}