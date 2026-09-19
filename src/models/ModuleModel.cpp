#include "ModuleModel.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>

ModuleModel::ModuleModel(QObject *parent) : QAbstractListModel(parent)
{
}

int ModuleModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.count();
}

QVariant ModuleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.count())
        return QVariant();

    const ModuleItem &item = m_items[index.row()];

    switch (role)
    {
    case NameRole:
        return item.name;
    case PathRole:
        return item.path;
    case ColorRole:
        return item.color;
    case CharacterRole:
        return item.character;
    }
    return QVariant();
}

QHash<int, QByteArray> ModuleModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[PathRole] = "path";
    roles[ColorRole] = "color";
    roles[CharacterRole] = "character";
    return roles;
}

void ModuleModel::loadDataFromSource()
{
    QString configPath = "config.json";
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "无法打开配置文件，请检查路径及权限:" << configPath;
        return;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject())
    {
        qWarning() << "JSON 解析失败或格式不正确";
        return;
    }

    QJsonObject rootObj = jsonDoc.object();
    if (!rootObj.contains("modules") || !rootObj["modules"].isArray())
    {
        qWarning() << "JSON 中未找到有效的 'modules' 数组节点";
        return;
    }

    QJsonArray modulesArray = rootObj["modules"].toArray();

    beginResetModel();
    m_items.clear();

    for (int i = 0; i < modulesArray.size(); ++i)
    {
        QJsonObject itemObj = modulesArray[i].toObject();

        QString name = itemObj.value("name").toString("未命名");
        QString color = itemObj.value("color").toString("#3B82F6");

        // 若无 "path" 键，返回空字符串 ""
        QString path = itemObj.contains("path") ? itemObj.value("path").toString() : QString();

        // 动态回退生成 character（取 name 首字母）
        QString character = name.isEmpty() ? QStringLiteral("-") : name.left(1).toUpper();

        m_items.append({name, path, color, character});
    }
    endResetModel();

    qDebug() << "本地配置加载完成，共读取" << m_items.count() << "个模块";
}

// 统一保存内存中的 m_items 到 config.json
bool ModuleModel::saveToFile()
{
    const QString configPath = "config.json";
    QJsonObject rootObj;

    // 1. 若原文件已存在且有效，保留原有的外部字段（例如 version 或其他全局配置）
    QFile readFile(configPath);
    if (readFile.exists() && readFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(readFile.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
        {
            rootObj = doc.object();
        }
        readFile.close();
    }

    // 2. 构建 modules 节点
    QJsonArray modulesArray;
    for (const auto &item : m_items)
    {
        QJsonObject obj;

        // 【必填项 1】name：确保键存在，若为空字符串则做兜底处理
        obj["name"] = item.name.trimmed().isEmpty() ? QStringLiteral("未命名") : item.name;

        // 【必填项 2】color：确保键存在，非法或空时提供默认色
        obj["color"] = item.color.trimmed().isEmpty() ? QStringLiteral("#3B82F6") : item.color;

        // 【必填项 3】character：确保键存在，空值时回退到名称首字符或缺省符
        if (!item.character.trimmed().isEmpty())
        {
            obj["character"] = item.character;
        }
        else if (!item.name.trimmed().isEmpty())
        {
            obj["character"] = item.name.trimmed().left(1).toUpper();
        }
        else
        {
            obj["character"] = QStringLiteral("-");
        }

        // 【可选项】path：仅在路径实际存在（非全空格、非空）时才序列化该键
        QString trimmedPath = item.path.trimmed();
        if (!trimmedPath.isEmpty())
        {
            obj["path"] = trimmedPath;
        }

        modulesArray.append(obj);
    }

    // 更新业务元数据
    rootObj["version"] = "1.1";
    rootObj["modules"] = modulesArray;

    // 3. 使用 QSaveFile 进行安全写入（写入临时文件并在 commit 时原子重命名）
    QSaveFile saveFile(configPath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "[ModuleModel] 无法创建或打开配置文件用于保存:" << configPath;
        return false;
    }

    // 使用 Indented 格式化缩进，方便人工查看与编辑
    QByteArray outputData = QJsonDocument(rootObj).toJson(QJsonDocument::Indented);
    qint64 bytesWritten = saveFile.write(outputData);

    if (bytesWritten == -1 || !saveFile.commit())
    {
        qWarning() << "[ModuleModel] 文件写入提交失败，可能磁盘空间不足或权限异常";
        saveFile.cancelWriting();
        return false;
    }

    return true;
}

// 【核心修改接口】更新指定项并通知 QML
bool ModuleModel::updateModule(int index, const QString &name, const QString &path, const QString &color, const QString &character)
{
    if (index < 0 || index >= m_items.count())
    {
        qWarning() << "[ModuleModel] 修改失败：索引越界" << index;
        return false;
    }

    // 1. 更新内存数据
    m_items[index] = {name, path, color, character};

    // 2. 发射 dataChanged 信号，精确刷新 4 个 Role
    QModelIndex modelIdx = createIndex(index, 0);
    emit dataChanged(modelIdx, modelIdx, {NameRole, PathRole, ColorRole, CharacterRole});

    // 3. 持久化到 JSON 文件
    bool ok = saveToFile();
    if (ok)
    {
        qDebug() << "[ModuleModel] 模块更新成功并写入文件，索引:" << index;
    }
    return ok;
}

// 【新增删除接口】
bool ModuleModel::removeModule(int index)
{
    if (index < 0 || index >= m_items.count())
    {
        qWarning() << "[ModuleModel] 删除失败：索引越界" << index;
        return false;
    }

    beginRemoveRows(QModelIndex(), index, index);
    m_items.removeAt(index);
    endRemoveRows();

    return saveToFile();
}

void ModuleModel::addModule(const QString &name, const QString &path, const QString &color, const QString &character)
{
    beginInsertRows(QModelIndex(), m_items.count(), m_items.count());
    m_items.append({name, path, color, character});
    endInsertRows();

    saveToFile();
}