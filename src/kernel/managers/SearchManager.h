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
#ifndef SEARCHMANAGER_H
#define SEARCHMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QTimer>
#include <atomic>
#include <mc_search.h>

class QThread;
class QThreadPool;

// Shared state for one in-flight download, written by the worker thread
// (atomics only) and read by the GUI thread.
struct SearchTaskState {
    int modelIndex = -1;
    QString id;
    QString name;
    QString fileName;
    QString url;
    QString sha1;
    QString rootDir;
    QString type;   // "shader" / "datapack" / "resourcepack"
    long long expectedSize = 0;
    std::atomic<int> started{0};
    std::atomic<int> done{0};        // 0=running, 1=ok, 2=failed
    std::atomic<int> cancelled{0};
    std::atomic<long long> received{0};
    std::atomic<long long> total{0};
    long long lastSampleReceived = 0;
    qreal speedBytes = 0.0;
    QThread *thread = nullptr;
};

class SearchManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY tasksChanged)

public:
    explicit SearchManager(QObject *parent = nullptr);
    ~SearchManager() override;

    bool searching() const { return m_searching; }
    QVariantList tasks() const { return m_tasks; }
    bool busy() const;

    Q_INVOKABLE void search(const QString &query, const QString &sort, int limit,
                            const QString &mcVersion, const QString &loader,
                            int offset, const QString &source,
                            const QString &type);
    Q_INVOKABLE void getProject(const QString &projectId, const QString &source,
                                 const QString &type);
    Q_INVOKABLE void getFiles(const QString &projectId, const QString &type);
    Q_INVOKABLE void installResult(const QVariantMap &result, const QString &rootDir);
    Q_INVOKABLE void cancelTask(int index);
    Q_INVOKABLE void retryTask(int index);
    Q_INVOKABLE void clearFinished();

signals:
    void searchingChanged();
    void tasksChanged();
    void searchCompleted(const QVariantList &results);
    // Emitted with the search type so pages can filter out irrelevant results.
    void searchResultReady(const QVariantList &results, const QString &type);
    void projectLoaded(const QVariantMap &project);
    void filesLoaded(const QVariantList &files);
    void installCompleted(bool success, const QString &path);
    void errorOccurred(const QString &message);

private:
    void pump();
    void startTask(const std::shared_ptr<SearchTaskState> &st);
    void tick();
    bool hasActive() const;
    QString installDir(const QString &rootDir, const QString &type) const;

    QThreadPool *m_workerPool = nullptr;
    bool m_searching = false;
    QVariantList m_tasks;
    QVector<std::shared_ptr<SearchTaskState>> m_taskStates;
    QTimer m_tickTimer;
};

#endif
