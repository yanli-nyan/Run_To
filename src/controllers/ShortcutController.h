#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
class ShortcutController : public QObject
{
Q_OBJECT Q_PROPERTY(QString filePath READ filePath CONSTANT) public : explicit ShortcutController(QObject *parent = nullptr);
    QString filePath() const;
    Q_INVOKABLE QVariantList loadShortcuts();
    Q_INVOKABLE bool saveShortcuts(const QVariantList &shortcuts);
    Q_INVOKABLE bool executeShortcut(const QString &keyCode);
signals:
    void errorOccurred(const QString &message);

private:
    QVariantList defaultShortcuts() const;
};
