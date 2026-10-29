#ifndef MODULEMODEL_H
#define MODULEMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "ConfigPaths.h"

struct ModuleItem
{
    QString name;
    QString path;
    QString color;
    QString character;
};

class ModuleModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum ModuleRoles
    {
        NameRole = Qt::UserRole + 1,
        PathRole,
        ColorRole,
        CharacterRole
    };

    explicit ModuleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 数据加载与持久化
    // 确保配置文件目录（exe 上一级目录下的 config 文件夹）已存在
    Q_INVOKABLE bool ensureConfigDir();
    Q_INVOKABLE void loadDataFromSource();
    Q_INVOKABLE void addModule(const QString &name, const QString &path, const QString &color, const QString &character);

    // 【新增】修改指定索引的 4 个角色属性
    Q_INVOKABLE bool updateModule(int index, const QString &name, const QString &path, const QString &color, const QString &character);

    // 【新增】删除指定索引的项（配套 Main.qml 中的 onDeleted）
    Q_INVOKABLE bool removeModule(int index);

private:
    QList<ModuleItem> m_items;
    // 配置文件统一存放于 exe 上一级目录的 config 文件夹中
    QString configFilePath() const;
    bool saveToFile(); // 统一写入 config.json
};

#endif // MODULEMODEL_H