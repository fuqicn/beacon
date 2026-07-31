#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QVariant>
#include <QVariantMap>

class SettingsManager : public QObject
{
    Q_OBJECT
public:
    explicit SettingsManager(QObject *parent = nullptr);

    void setLauncherDir(const QString &dir);

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void beginGroup(const QString &prefix);
    Q_INVOKABLE void endGroup();
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE bool contains(const QString &key) const;

    Q_INVOKABLE QVariantMap detectSystemSettings() const;

    Q_INVOKABLE void setInstance(const QString &instanceId);
    Q_INVOKABLE void endInstance();

private:
    QSettings *m_launcherSettings = nullptr;
    QSettings *m_instanceSettings = nullptr;
    QSettings *m_active = nullptr;
    QString m_launcherDir;
};

#endif
