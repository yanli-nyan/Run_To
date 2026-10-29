#pragma once

#include <QObject>
#include <QString>

/**
 * @brief 在系统文件管理器中定位并选中指定文件。
 *
 * 供 DetailPopup 的“打开位置”按钮调用：把弹窗里显示的执行路径
 * 直接在资源管理器里选中，省去用户手工逐级查找。
 */
class FileRevealer : public QObject
{
    Q_OBJECT
public:
    explicit FileRevealer(QObject *parent = nullptr);

    /// 路径是否指向真实存在的文件或目录（QML 用它决定按钮是否可用）。
    Q_INVOKABLE bool exists(const QString &targetPath);

    /// 在资源管理器中选中该文件；目标不存在时退化为打开其所在目录。
    Q_INVOKABLE bool reveal(const QString &targetPath);
};
