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
                       const std::function<void(qreal, const QString &)> &progress);

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
