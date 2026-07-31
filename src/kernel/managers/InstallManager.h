#ifndef INSTALLMANAGER_H
#define INSTALLMANAGER_H

#include <QObject>
#include <QString>
#include <QThread>

class InstallWorker;

class InstallManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    explicit InstallManager(QObject *parent = nullptr);
    ~InstallManager() override;

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    qreal progress() const { return m_progress; }

    Q_INVOKABLE void installLoader(const QString &mcVersion, const QString &loader,
                                   const QString &loaderVer, const QString &javaPath,
                                   const QString &dir);

    Q_INVOKABLE void fetchLoaderVersions(const QString &mcVersion, const QString &loader);

    Q_INVOKABLE void cancelAll();

signals:
    void busyChanged();
    void statusChanged(const QString &status);
    void progressChanged(qreal progress);
    void installCompleted(const QString &versionId);
    void errorOccurred(const QString &message);
    void loaderVersionsReady(const QStringList &versions, const QString &loader);

private:
    void setBusy(bool b);

    bool m_busy = false;
    QString m_status;
    qreal m_progress = 0.0;
    QThread *m_workerThread = nullptr;
    InstallWorker *m_worker = nullptr;
};

#endif
