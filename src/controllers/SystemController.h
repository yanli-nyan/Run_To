#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief 本机系统动作执行器（电源、锁屏、媒体、剪贴板）
 *
 * 所有方法均为同步调用，返回是否已成功下发；失败原因通过 detail 回传。
 * Windows 专用实现，非 Windows 平台统一返回 false 并给出说明。
 */
class SystemController : public QObject
{
    Q_OBJECT
public:
    explicit SystemController(QObject *parent = nullptr);

    /** 计划重启，delaySec 为倒计时秒数 */
    bool restart(int delaySec, QString &detail);
    /** 计划关机，delaySec 为倒计时秒数 */
    bool shutdown(int delaySec, QString &detail);
    /** 取消已计划的关机/重启（shutdown /a） */
    bool cancelShutdown(QString &detail);
    /** 锁定工作站 */
    bool lockWorkstation(QString &detail);
    /** 进入睡眠 */
    bool sleep(QString &detail);
    /** 写入剪贴板文本 */
    bool setClipboardText(const QString &text, QString &detail);
    /** 媒体控制：mute / play_pause / volume_up / volume_down */
    bool mediaControl(const QString &op, QString &detail);

private:
    /** 以逐参数方式调用 shutdown.exe，避免拼接命令行被解析成额外参数 */
    bool runShutdown(const QStringList &args, QString &detail);
};
