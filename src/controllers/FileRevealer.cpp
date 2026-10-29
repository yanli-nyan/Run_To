#include "FileRevealer.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QUrl>

FileRevealer::FileRevealer(QObject *parent) : QObject(parent) {}

bool FileRevealer::exists(const QString &targetPath)
{
    const QString cleanPath = QDir::cleanPath(targetPath.trimmed());
    if (cleanPath.isEmpty())
    {
        return false;
    }

    const QFileInfo info(cleanPath);
    return info.exists() && (info.isFile() || info.isDir());
}

bool FileRevealer::reveal(const QString &targetPath)
{
    const QString cleanPath = QDir::cleanPath(targetPath.trimmed());
    if (cleanPath.isEmpty())
    {
        qWarning() << "[FileRevealer] 没有可定位的路径";
        return false;
    }

    const QFileInfo info(cleanPath);

    // 目标已不存在（例如程序被卸载或移动）时退化为打开其所在目录，
    // 这样用户仍能看到该目录下的内容，而不是直接失败。
    if (!info.exists())
    {
        const QString fallbackDir = info.absolutePath();
        if (!QFileInfo(fallbackDir).isDir())
        {
            qWarning() << "[FileRevealer] 文件不存在 ->" << cleanPath;
            return false;
        }

        qWarning() << "[FileRevealer] 目标不存在，改为打开所在目录 ->" << fallbackDir;
        return QDesktopServices::openUrl(QUrl::fromLocalFile(fallbackDir));
    }

#if defined(Q_OS_WIN)
    // 目录直接交给资源管理器打开。
    if (info.isDir())
    {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    }

    // 文件用 “/select,” 参数交给资源管理器选中，
    // 与 Windows 右键菜单里的“打开文件所在的位置”行为一致。
    const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    const QStringList arguments{QStringLiteral("/select,") + nativePath};
    const bool started = QProcess::startDetached(QStringLiteral("explorer.exe"), arguments);
    if (!started)
    {
        qWarning() << "[FileRevealer] 无法在资源管理器中定位 ->" << nativePath;
    }
    return started;
#else
    // 其他平台没有统一的“选中文件”接口，退化为打开所在目录。
    const QString targetDir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(targetDir));
#endif
}
