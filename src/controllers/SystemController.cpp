#include "SystemController.h"

#include <QProcess>
#include <QClipboard>
#include <QGuiApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

SystemController::SystemController(QObject *parent) : QObject(parent) {}

bool SystemController::runShutdown(const QStringList &args, QString &detail)
{
#ifdef Q_OS_WIN
    QProcess proc;
    // 逐参数传入 QStringList，不拼接命令行字符串：
    // 注释文本里一旦出现引号或空格，拼接写法会把后半段当成新的开关。
    proc.start(QStringLiteral("shutdown.exe"), args);
    if (!proc.waitForStarted(2000))
    {
        detail = QStringLiteral("无法启动 shutdown.exe: %1").arg(proc.errorString());
        return false;
    }
    // shutdown.exe 下发计划后立即返回，这里等待它是为了拿到真实退出码
    if (!proc.waitForFinished(5000))
    {
        proc.kill();
        detail = QStringLiteral("shutdown.exe 执行超时");
        return false;
    }
    const int code = proc.exitCode();
    if (code != 0)
    {
        QString stdErr = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        detail = QStringLiteral("shutdown.exe 退出码 %1%2").arg(code).arg(
            stdErr.isEmpty() ? QString() : QStringLiteral("，输出: ") + stdErr);
        return false;
    }
    return true;
#else
    Q_UNUSED(args);
    detail = QStringLiteral("当前平台不支持电源操作");
    return false;
#endif
}

bool SystemController::restart(int delaySec, QString &detail)
{
    qInfo() << "[SystemController] 计划重启，延迟" << delaySec << "秒";
    return runShutdown(QStringList{
        QStringLiteral("/r"),
        QStringLiteral("/t"),
        QString::number(delaySec),
        // 保持纯 ASCII 注释，避免不同代码页下被 shutdown.exe 误读
        QStringLiteral("/c"),
        QStringLiteral("Run_To: remote restart command"),
        QStringLiteral("/d"),
        QStringLiteral("p:0:0")},
        detail);
}

bool SystemController::shutdown(int delaySec, QString &detail)
{
    qInfo() << "[SystemController] 计划关机，延迟" << delaySec << "秒";
    return runShutdown(QStringList{
        QStringLiteral("/s"),
        QStringLiteral("/t"),
        QString::number(delaySec),
        QStringLiteral("/c"),
        QStringLiteral("Run_To: remote shutdown command")},
        detail);
}

bool SystemController::cancelShutdown(QString &detail)
{
    qInfo() << "[SystemController] 取消已计划的关机/重启";
    return runShutdown(QStringList{QStringLiteral("/a")}, detail);
}

bool SystemController::lockWorkstation(QString &detail)
{
#ifdef Q_OS_WIN
    QProcess proc;
    proc.start(QStringLiteral("rundll32.exe"),
               QStringList{QStringLiteral("user32.dll,LockWorkStation")});
    if (!proc.waitForStarted(2000) || !proc.waitForFinished(5000))
    {
        detail = QStringLiteral("锁屏调用失败: %1").arg(proc.errorString());
        return false;
    }
    detail = QStringLiteral("已锁定工作站");
    return true;
#else
    detail = QStringLiteral("当前平台不支持锁屏");
    return false;
#endif
}

bool SystemController::sleep(QString &detail)
{
#ifdef Q_OS_WIN
    QProcess proc;
    // 参数 0=睡眠 1=强制 0=不触发唤醒事件；若系统关闭了睡眠会转为休眠
    proc.start(QStringLiteral("rundll32.exe"),
               QStringList{QStringLiteral("powrprof.dll,SetSuspendState"),
                           QStringLiteral("0,1,0")});
    if (!proc.waitForStarted(2000) || !proc.waitForFinished(5000))
    {
        detail = QStringLiteral("睡眠调用失败: %1").arg(proc.errorString());
        return false;
    }
    detail = QStringLiteral("已请求进入睡眠");
    return true;
#else
    detail = QStringLiteral("当前平台不支持睡眠");
    return false;
#endif
}

bool SystemController::setClipboardText(const QString &text, QString &detail)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
    {
        detail = QStringLiteral("无法访问剪贴板");
        return false;
    }
    clipboard->setText(text);
    detail = QStringLiteral("已写入剪贴板，长度 %1").arg(text.size());
    return true;
}

bool SystemController::mediaControl(const QString &op, QString &detail)
{
#ifdef Q_OS_WIN
    WORD key = 0;
    if (op == QStringLiteral("mute"))
        key = VK_VOLUME_MUTE;
    else if (op == QStringLiteral("play_pause"))
        key = VK_MEDIA_PLAY_PAUSE;
    else if (op == QStringLiteral("volume_up"))
        key = VK_VOLUME_UP;
    else if (op == QStringLiteral("volume_down"))
        key = VK_VOLUME_DOWN;
    else
    {
        detail = QStringLiteral("不支持的媒体操作: %1").arg(op);
        return false;
    }

    INPUT input[2]{};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[1].ki.wVk = key;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, input, sizeof(INPUT)) != 2)
    {
        detail = QStringLiteral("SendInput 调用失败，操作码 %1").arg(key);
        return false;
    }
    detail = QStringLiteral("已执行媒体操作: %1").arg(op);
    return true;
#else
    Q_UNUSED(op);
    detail = QStringLiteral("当前平台不支持媒体控制");
    return false;
#endif
}
