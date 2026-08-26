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
#ifndef MODMANAGER_H
#define MODMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QTimer>
#include <memory>
#include <atomic>
#include <mc_mod.h>

class QThread;
class QThreadPool;
class ModWorker;

// Shared state for one in-flight download. Written by the worker thread
// (atomics only) and read/owned by the GUI thread.
struct ModTaskState {
    int modelIndex = -1;
    QString id;
    QString name;
    QString fileName;
    QString url;
    QString sha1;
    QString rootDir;
    long long expectedSize = 0;
    std::atomic<int> started{0};     // 1 while the worker thread is running
    std::atomic<int> done{0};        // 0 running, 1 ok, 2 failed
    std::atomic<int> cancelled{0};
    std::atomic<long long> received{0};
    std::atomic<long long> total{0};
    // Main-thread-only download-speed bookkeeping (not atomic).
    long long lastSampleReceived = 0;
    qreal speedBytes = 0.0;
    QThread *thread = nullptr;       // owned via finished -> deleteLater
};

class ModManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(bool searchingPacks READ searchingPacks NOTIFY searchingPacksChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY tasksChanged)

public:
    explicit ModManager(QObject *parent = nullptr);
    ~ModManager() override;

    bool searching() const { return m_searching; }
    bool searchingPacks() const { return m_searchingPacks; }
    QVariantList tasks() const { return m_tasks; }
    bool busy() const;

    Q_INVOKABLE void search(const QString &query, const QString &sort = "relevance",
                            int limit = 20,
                            const QString &mcVersion = QString(),
                            const QString &loader = QString(),
                            int offset = 0);
    Q_INVOKABLE void searchPacks(const QString &query, const QString &sort = "relevance",
                                 int limit = 20,
                                 const QString &mcVersion = QString(),
                                 const QString &loader = QString(),
                                 int offset = 0);
    Q_INVOKABLE void getProject(const QString &projectId);
    Q_INVOKABLE void getVersions(const QString &projectId,
                                 const QString &mcVersion = QString(),
                                 const QString &loader = QString());
    Q_INVOKABLE void getProjects(const QStringList &ids);

    // Enqueue a mod file download. Multiple files download concurrently
    // (see kMaxConcurrent). Returns the new task's id ("" on invalid input).
    Q_INVOKABLE QString installMod(const QVariantMap &file, const QString &rootDir);
    Q_INVOKABLE void cancelTask(int index);
    Q_INVOKABLE void retryTask(int index);
    Q_INVOKABLE void clearFinished();

    Q_INVOKABLE QVariantList listInstalledMods(const QString &rootDir) const;
    Q_INVOKABLE bool removeMod(const QString &rootDir, const QString &fileName);
    Q_INVOKABLE void openModsFolder(const QString &rootDir);

signals:
    void searchingChanged();
    void searchingPacksChanged();
    void tasksChanged();
    void searchCompleted(const QVariantList &results);
    void packSearchCompleted(const QVariantList &results);
    void projectLoaded(const QVariantMap &project);
    void versionsLoaded(const QVariantList &versions);
    void projectsLoaded(const QVariantList &projects);
    void installCompleted(bool success, const QString &path);
    void errorOccurred(const QString &message);

private:
    void pump();
    void startTask(const std::shared_ptr<ModTaskState> &st);
    void tick();
    bool hasActive() const;
    QString modsDir(const QString &rootDir) const;

    QThreadPool *m_workerPool = nullptr;
    ModWorker *m_worker = nullptr;
    bool m_searching = false;
    bool m_searchingPacks = false;
    QVariantList m_tasks;
    QVector<std::shared_ptr<ModTaskState>> m_taskStates;
    QTimer m_tickTimer;
};

#endif
