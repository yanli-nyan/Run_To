#pragma once

#include <QObject>
#include <QString>

/**
 * @brief 开机自启动开关（Windows 注册表实现）
 *
 * 事实来源是 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 下的
 * "Run_To" 值；同时在 StartupApproved\Run 写入启用/禁用状态，让任务管理器
 * 的「启动」页与本程序设置界面保持一致。
 *
 * 首次运行默认开启；用户在界面里显式关闭后，会在 QSettings 中记录
 * autostart/userDisabled=true，否则每次启动又会把 Run 值补写回来。
 */
class AutoStartManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)

public:
    explicit AutoStartManager(QObject *parent = nullptr);

    /** 当前是否已登记开机自启（Run 值存在且未被用户显式关闭） */
    bool enabled() const;

    /** 供 QML 调用：开启/关闭自启动，返回是否写入成功 */
    Q_INVOKABLE bool setEnabled(bool on);

    /** 程序启动时调用：首次运行默认开启，路径失效或指向旧副本时自动纠正 */
    void reconcileOnStartup();

signals:
    void enabledChanged();
};
