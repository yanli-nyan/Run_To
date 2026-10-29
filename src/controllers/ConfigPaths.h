#pragma once

// 配置文件路径统一管理。
//
// 约定：所有运行时配置与导出产物都存放在「exe 所在目录的上一级目录」
// 下的 config 文件夹中。
// 例如 exe 位于 C:/App/bin/Run_To.exe，则配置目录为 C:/App/config，
// config.json / key.json / share_bundle.zip 均位于该目录。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace ConfigPaths
{
// exe 所在目录的上一级目录
inline QDir baseDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir;
}

// exe 上一级目录下的 config 文件夹
inline QString configDir()
{
    return baseDir().filePath(QStringLiteral("config"));
}

// config 文件夹中的具体文件
inline QString filePath(const QString &fileName)
{
    return QDir(configDir()).filePath(fileName);
}

// 确保 config 文件夹存在（首次启动时自动创建）
inline bool ensureDir()
{
    QDir dir(configDir());
    if (dir.exists())
    {
        return true;
    }
    return QDir().mkpath(configDir());
}

// 兼容旧版本：配置文件原先生成在 exe 同目录。
// 仅当新位置不存在、旧位置存在时迁移一次，避免老用户丢失已有配置。
inline void migrateLegacyFile(const QString &fileName)
{
    const QString target = filePath(fileName);
    if (QFileInfo::exists(target))
    {
        return;
    }

    const QString legacy = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
    if (!QFileInfo::exists(legacy))
    {
        return;
    }

    ensureDir();
    if (!QFile::rename(legacy, target))
    {
        // 跨卷或文件被占用导致重命名失败时退化为复制
        if (QFile::copy(legacy, target))
        {
            QFile::remove(legacy);
        }
    }
}
}
