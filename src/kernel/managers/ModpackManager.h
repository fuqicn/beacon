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
#ifndef MODPACKMANAGER_H
#define MODPACKMANAGER_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>
#include <functional>

class ModpackWorker;

// Installs Modrinth modpacks (.mrpack). Supports both local files and
// Modrinth project files. Each install runs in its own worker thread and
// produces an isolated instance (game files inside versions/<id>).
class ModpackManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    explicit ModpackManager(QObject *parent = nullptr);
    ~ModpackManager() override;

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    qreal progress() const { return m_progress; }

    Q_INVOKABLE void installFromFile(const QString &filePath, const QString &rootDir);
    Q_INVOKABLE void installFromProject(const QVariantMap &file, const QString &rootDir);
    Q_INVOKABLE void cancelAll();

signals:
    void busyChanged();
    void statusChanged(const QString &status);
    void progressChanged(qreal progress);
    void installCompleted(const QString &instanceId);
    void errorOccurred(const QString &message);

private:
    void setBusy(bool b);
    void stopWorkerThread();

    static void startPackInstall(ModpackManager *self, ModpackWorker *worker,
                                 const std::function<void()> &start);

    bool m_busy = false;
    QString m_status;
    qreal m_progress = 0.0;
    QThread *m_workerThread = nullptr;
    ModpackWorker *m_worker = nullptr;
    // True while a cooperative cancel has been requested for the current
    // worker. Used to pair the ref-counted global download cancel flag with a
    // matching release exactly once, when the worker thread actually exits.
    bool m_cancelRequested = false;
};

#endif
