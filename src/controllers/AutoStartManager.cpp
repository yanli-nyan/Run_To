#include "AutoStartManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
#ifdef Q_OS_WIN
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"Run_To";

// 自启动命令行里额外带上的参数：被开机拉起时直接驻留托盘，不弹主界面
constexpr char kStartupArg[] = "--startup";

QString readRunValue()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return QString();
    }

    wchar_t buffer[2048] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS || type != REG_SZ)
    {
        return QString();
    }
    return QString::fromWCharArray(buffer);
}

bool writeRunValue(const QString &command)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                   reinterpret_cast<const BYTE *>(command.utf16()), bytes);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

void deleteRunValue()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    {
        return;
    }
    RegDeleteValueW(key, kValueName);
    RegCloseKey(key);
}

// 读取 StartupApproved\Run 的启用状态：真=启用、假=被禁用
bool readApprovedState()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return true;
    }

    BYTE buffer[64] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type, buffer, &size);
    RegCloseKey(key);

    // 未记录该值时不视为禁用；0x03 才是"已禁用"
    if (rc != ERROR_SUCCESS || (type != REG_BINARY && type != REG_DWORD) || size < 1)
    {
        return true;
    }
    return buffer[0] != 0x03;
}

// Windows 在 StartupApproved\Run 中用 12 字节二进制记录启用状态：
// 首字节 0x02 表示启用、0x03 表示被用户/系统禁用，后 8 字节是 FILETIME。
bool writeApprovedState(bool on)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    QByteArray data(12, '\0');
    data[0] = on ? static_cast<char>(0x02) : static_cast<char>(0x03);
    const LONG rc = RegSetValueExW(key, kValueName, 0, REG_BINARY,
                                   reinterpret_cast<const BYTE *>(data.constData()),
                                   static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}
#else
QString readRunValue() { return QString(); }
bool writeRunValue(const QString &) { return false; }
void deleteRunValue() {}
bool readApprovedState() { return true; }
bool writeApprovedState(bool) { return false; }
#endif

// 开机自启要写入的命令行："<当前 exe 的绝对路径>" --startup
QString expectedCommand()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\" %2").arg(exe, QString::fromLatin1(kStartupArg));
}

// 从注册表值里取出可执行文件路径（去掉外层引号与后续参数）
QString commandTarget(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
    {
        return QString();
    }

    if (trimmed.startsWith(QLatin1Char('"')))
    {
        const int end = trimmed.indexOf(QLatin1Char('"'), 1);
        if (end > 1)
        {
            return QDir::fromNativeSeparators(trimmed.mid(1, end - 1));
        }
        return QString();
    }

    return QDir::fromNativeSeparators(trimmed.section(QLatin1Char(' '), 0, 0));
}

// 统一成小写、无分隔符差异的绝对路径，用于 Windows 下的大小写不敏感比较
QString canonicalPath(const QString &path)
{
    if (path.isEmpty())
    {
        return QString();
    }

    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    const QString absolute = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    return QDir::cleanPath(absolute).toLower();
}

bool sameTarget(const QString &command, const QString &expected)
{
    return canonicalPath(commandTarget(command)) == canonicalPath(commandTarget(expected));
}

// 用户是否在设置界面里显式关闭过自启
bool userDisabledFlag()
{
    return QSettings().value(QStringLiteral("autostart/userDisabled"), false).toBool();
}

void setUserDisabledFlag(bool disabled)
{
    QSettings settings;
    if (disabled)
    {
        settings.setValue(QStringLiteral("autostart/userDisabled"), true);
    }
    else
    {
        settings.remove(QStringLiteral("autostart/userDisabled"));
    }
}
}

AutoStartManager::AutoStartManager(QObject *parent)
    : QObject(parent)
{
}

bool AutoStartManager::enabled() const
{
    // 以注册表为准：关闭自启时会删除 Run 值，因此这里读到空即表示未启用。
    // userDisabledFlag 只用于「阻止下次启动把自启又加回来」，不参与状态展示，
    // 这样用户在任务管理器里手动重新启用后，界面也能如实反映。
    if (readRunValue().isEmpty())
    {
        return false;
    }

    // Run 值存在但被任务管理器/优化工具禁用（StartupApproved 首字节 0x03）时，
    // 开机同样不会启动，界面必须如实显示为「已关闭」。
    return readApprovedState();
}

bool AutoStartManager::setEnabled(bool on)
{
    bool ok = true;

    if (on)
    {
        ok = writeRunValue(expectedCommand());
        if (ok)
        {
            // 解除任务管理器启动页里可能存在的"已禁用"标记
            writeApprovedState(true);
            setUserDisabledFlag(false);
        }
    }
    else
    {
        deleteRunValue();
        writeApprovedState(false);
        setUserDisabledFlag(true);
    }

    emit enabledChanged();
    return ok;
}

void AutoStartManager::reconcileOnStartup()
{
    if (userDisabledFlag())
    {
        // 用户关闭过，就不该被"默认开启"策略重新加回来
        if (!readRunValue().isEmpty())
        {
            deleteRunValue();
            emit enabledChanged();
        }
        return;
    }

    const QString current = readRunValue();
    const QString expected = expectedCommand();

    // 未登记（首次运行）或登记的是失效路径/旧副本时，按当前 exe 纠正
    if (current.isEmpty() || !sameTarget(current, expected))
    {
        if (writeRunValue(expected))
        {
            writeApprovedState(true);
            emit enabledChanged();
        }
        return;
    }

    // Run 值正确但启动项被禁用（例如被优化工具关闭）时恢复启用状态，
    // 否则界面显示「已开启」而开机实际不会运行。
    if (!readApprovedState() && writeApprovedState(true))
    {
        emit enabledChanged();
    }
}
