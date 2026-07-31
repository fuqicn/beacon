#ifndef JAVAMANAGER_H
#define JAVAMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <mc_java.h>
#include <mc_java_dl.h>

class JavaManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList runtimes READ runtimes NOTIFY runtimesChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString runtimeDir READ runtimeDir WRITE setRuntimeDir NOTIFY runtimeDirChanged)

public:
    explicit JavaManager(QObject *parent = nullptr);
    ~JavaManager() override;

    QVariantList runtimes() const { return m_runtimes; }
    bool searching() const { return m_searching; }
    QString runtimeDir() const { return m_runtimeDir; }
    void setRuntimeDir(const QString &dir);

    Q_INVOKABLE void findJava();
    Q_INVOKABLE QVariantMap findBest() const;
    Q_INVOKABLE void findJavaAsync();
    Q_INVOKABLE void downloadJava(int majorVersion);

signals:
    void runtimesChanged();
    void searchingChanged();
    void runtimeDirChanged();
    void javaDownloaded(const QString &path, int majorVersion);
    void javaDownloadFailed(int majorVersion);
    void errorOccurred(const QString &message);

private:
    void doFindJava();
    void doDownloadJava(int majorVersion);
    void addBundledJava();

    QVariantList m_runtimes;
    bool m_searching = false;
    QString m_runtimeDir;
};

#endif
