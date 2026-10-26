/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef INSTALLMANAGER_H
#define INSTALLMANAGER_H

#include <QObject>
#include <QString>
#include <QThread>
#include <functional>

class InstallWorker;

// Synchronous loader installer (installs loader profile/libs/base version into dir).
// Returns true on success; on failure sets *errorOut. *outVerId receives the
// installed version id (may be null). progress may be null.
bool installLoaderSync(const QString &mcVersion, const QString &loader,
                       const QString &loaderVer, const QString &javaPath,
                       const QString &dir, QString *errorOut, QString *outVerId,
                       const std::function<void(qreal, const QString &)> &progress,
                       const QString &customName = QString());

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
    // Target directory of the most recent loader install. Used by KernelBridge
    // to continue with the vanilla libraries/assets download once the loader
    // profile has been written.
    QString targetDir() const { return m_targetDir; }

    Q_INVOKABLE void installLoader(const QString &mcVersion, const QString &loader,
                                    const QString &loaderVer, const QString &javaPath,
                                    const QString &dir, const QString &customName = QString());

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
    // Dispatches the install worker. Forge needs a JVM resolved first, so the
    // caller either passes one or goes through resolveForgeJava().
    void startInstallWorker(const QString &mcVersion, const QString &loader,
                            const QString &loaderVer, const QString &javaPath,
                            const QString &dir, const QString &customName);
    // Finds a JVM for the official Forge installer without blocking the GUI
    // thread (bounded scan, then the kernel's deep scan on a low-priority
    // worker). Continues with startInstallWorker() or downloadJavaForForge().
    void resolveForgeJava(const QString &mcVersion, const QString &loader,
                          const QString &loaderVer, const QString &dir,
                          const QString &customName, quint64 token);
    // Makes sure a Java runtime exists for the Forge installer, reusing a
    // download that is already in flight and starting Java 17 when needed.
    void downloadJavaForForge(const QString &mcVersion, const QString &loader,
                              const QString &loaderVer, const QString &dir,
                              const QString &customName, quint64 token);

    bool m_busy = false;
    QString m_status;
    qreal m_progress = 0.0;
    QString m_targetDir;
    QThread *m_workerThread = nullptr;
    InstallWorker *m_worker = nullptr;
    // True once the worker has been dispatched for the current install attempt
    // (the Java preparation phase runs before it).
    bool m_installStarted = false;
    // Bumped for every install attempt. Java preparation completes
    // asynchronously, and a callback that lands after a cancel or a newer
    // install must be dropped instead of starting a stale worker.
    quint64 m_installToken = 0;
};

#endif
