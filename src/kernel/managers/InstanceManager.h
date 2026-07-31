#ifndef INSTANCEMANAGER_H
#define INSTANCEMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

struct InstanceInfo {
    QString id;
    QString name;
    QString type;       // release, snapshot, old_beta, old_alpha
    QString rootDir;    // root .minecraft directory
    QString verDir;     // version-specific directory
    QString jsonPath;
    QString jarPath;
    bool hasJar;
    int libraryCount;
    int javaMajorVersion;
    QString mainClass;
};

class InstanceManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList instances READ instances NOTIFY instancesChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedChanged)
    Q_PROPERTY(QStringList rootDirs READ rootDirs NOTIFY rootDirsChanged)

public:
    explicit InstanceManager(QObject *parent = nullptr);
    ~InstanceManager() override;

    QVariantList instances() const { return m_instances; }
    QString selectedId() const { return m_selectedId; }
    QStringList rootDirs() const { return m_rootDirs; }

    Q_INVOKABLE void scanInstances();
    Q_INVOKABLE void addRootDir(const QString &dir);
    Q_INVOKABLE void removeRootDir(const QString &dir);
    Q_INVOKABLE void removeInstance(const QString &id);
    Q_INVOKABLE void selectInstance(const QString &id);
    Q_INVOKABLE QVariantMap getSelectedInstance() const;
    Q_INVOKABLE void setDefaultRootDir(const QString &mcDir);

signals:
    void instancesChanged();
    void selectedChanged();
    void rootDirsChanged();

private:
    void loadConfig();
    void saveConfig();
    void scanDirectory(const QString &rootDir);

    QVariantList m_instances;
    QString m_selectedId;
    QStringList m_rootDirs;
    QString m_configPath;
};

#endif
