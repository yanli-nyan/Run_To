#include "AppLauncher.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QDesktopServices>
#include <QDebug>

AppLauncher::AppLauncher(QObject *parent) : QObject(parent) {}

bool AppLauncher::launch(const QString &targetPath,
                         const QStringList &arguments,
                         const QString &workingDir)
{
    if (targetPath.trimmed().isEmpty())
    {
        qWarning() << "[AppLauncher] 启动失败：路径为空";
        return false;
    }

    // 规范化路径分隔符
    QString cleanPath = QDir::cleanPath(targetPath);
    QFileInfo fileInfo(cleanPath);

    if (!fileInfo.exists())
    {
        qWarning() << "[AppLauncher] 启动失败：目标文件不存在 ->" << cleanPath;
        return false;
    }

    // 1. 特殊处理 Windows 快捷方式 (.lnk)
    // QProcess 无法直接 CreateProcess 启动 .lnk 文件；
    // 使用 QDesktopServices::openUrl 会调用系统底层 ShellExecuteEx 执行，保留其内嵌的参数与工作路径
    if (cleanPath.endsWith(".lnk", Qt::CaseInsensitive))
    {
        bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(cleanPath));
        if (!ok)
        {
            qWarning() << "[AppLauncher] 打开快捷方式失败 ->" << cleanPath;
        }
        return ok;
    }

    // 2. 工作目录默认取目标文件所在目录
    QString actualWorkingDir = workingDir;
    if (actualWorkingDir.isEmpty())
    {
        actualWorkingDir = fileInfo.absolutePath();
    }

    // 3. 脚本文件：CreateProcess 无法直接把 .ps1 当程序启动（返回 193 ERROR_BAD_EXE_FORMAT），
    //    .bat/.cmd 依赖 ShellExecute 兜底且拿不到工作目录与参数。
    //    这里统一显式交给对应的解释器，脚本路径与参数分开传递，空格不会被误切。
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QStringLiteral("ps1") || suffix == QStringLiteral("bat") ||
        suffix == QStringLiteral("cmd"))
    {
        QString program;
        QStringList fullArgs;
        if (suffix == QStringLiteral("ps1"))
        {
            program = QStringLiteral("powershell.exe");
            fullArgs << QStringLiteral("-NoProfile") << QStringLiteral("-ExecutionPolicy")
                     << QStringLiteral("Bypass") << QStringLiteral("-File") << cleanPath;
        }
        else
        {
            program = QStringLiteral("cmd.exe");
            fullArgs << QStringLiteral("/c") << cleanPath;
        }
        fullArgs.append(arguments);

        const bool scriptStarted = QProcess::startDetached(program, fullArgs, actualWorkingDir);
        if (!scriptStarted)
        {
            qWarning() << "[AppLauncher] 脚本启动失败 ->" << cleanPath;
        }
        return scriptStarted;
    }

    // 4. 处理普通可执行文件 (.exe)
    // 自动将工作目录设置为该程序所在目录，避免目标程序因相对路径找不到自身依赖的 DLL 或资源
    // 以独立进程模式分离运行（主程序退出不会影响被启动的软件）
    bool started = QProcess::startDetached(cleanPath, arguments, actualWorkingDir);
    if (!started)
    {
        qWarning() << "[AppLauncher] 进程启动失败 ->" << cleanPath;
    }
    return started;
}