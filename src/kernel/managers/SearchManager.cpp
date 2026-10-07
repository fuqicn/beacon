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
#include "SearchManager.h"
#include <mc_log.h>
#include <mc_download_qt.h>
#include <QThread>
#include <QThreadPool>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QUuid>

static const int kMaxConcurrent = 4;
static const int kTickMs = 150;

static int searchTypeFromString(const QString &s)
{
    QString key = s.trimmed().toLower();
    if (key == "shader")          return MC_SEARCH_TYPE_SHADER;
    if (key == "datapack")        return MC_SEARCH_TYPE_DATAPACK;
    if (key == "resourcepack")    return MC_SEARCH_TYPE_RESOURCEPACK;
    return MC_SEARCH_TYPE_RESOURCEPACK;
}

static int searchSortFromString(const QString &s)
{
    if (s == "downloads")  return MC_SEARCH_SORT_DOWNLOADS;
    if (s == "newest")     return MC_SEARCH_SORT_NEWEST;
    if (s == "updated")    return MC_SEARCH_SORT_UPDATED;
    return MC_SEARCH_SORT_RELEVANCE;
}

static int searchSourceFromString(const QString &s)
{
    QString key = s.trimmed().toLower();
    if (key == "curseforge") return MC_SEARCH_SOURCE_CURSEFORGE;
    if (key == "all")        return MC_SEARCH_SOURCE_ANY;
    return MC_SEARCH_SOURCE_MODRINTH;
}

static QVariantMap resultToVariant(const McSearchResult &r)
{
    QVariantMap m;
    m["id"]           = QString::fromUtf8(r.id);
    m["slug"]         = QString::fromUtf8(r.slug);
    m["name"]         = QString::fromUtf8(r.name);
    m["description"]  = QString::fromUtf8(r.description);
    m["logoUrl"]      = QString::fromUtf8(r.logo_url);
    m["size"]         = (qlonglong)r.size;
    m["downloadUrl"]  = QString::fromUtf8(r.download_url);
    m["gameVersions"] = QString::fromUtf8(r.game_versions);
    m["loaders"]      = QString::fromUtf8(r.loaders);
    m["projectType"]  = QString::fromUtf8(r.project_type);
    m["source"]       = QString::fromUtf8(r.source);
    m["downloadCount"] = r.download_count;
    return m;
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
class SearchWorker : public QObject
{
    Q_OBJECT
public:
    explicit SearchWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void doSearch(const QString &query, const QString &sort, int limit,
                  const QString &mcVersion, const QString &loader, int offset,
                  const QString &source, const QString &type)
    {
        int maxResults = qBound(1, limit, 100);
        std::vector<McSearchResult> results((size_t)maxResults);
        for (auto &r : results) mc_search_result_init(&r);

        mc_info("[Search] search begin q='%s' type=%s source=%s offset=%d",
                qPrintable(query), qPrintable(type), qPrintable(source), offset);

        int count = mc_search(
            query.isEmpty() ? nullptr : query.toUtf8().constData(),
            mcVersion.isEmpty() ? nullptr : mcVersion.toUtf8().constData(),
            loader.isEmpty() ? nullptr : loader.toUtf8().constData(),
            searchTypeFromString(type),
            searchSourceFromString(source),
            maxResults, qMax(offset, 0), searchSortFromString(sort),
            results.data(), maxResults);

        mc_info("[Search] search done type=%s(%d): %d results",
                qPrintable(type), searchTypeFromString(type), count);
        emit searchCompleted(resultsToVariant(results, count));
    }

signals:
    void searchCompleted(const QVariantList &results);

private:
    static QVariantList resultsToVariant(const std::vector<McSearchResult> &results, int count)
    {
        QVariantList list;
        for (int i = 0; i < count; ++i) {
            list.append(resultToVariant(results[i]));
            mc_search_result_free((McSearchResult *)&results[i]);
        }
        return list;
    }
};

#include "SearchManager.moc"

// ---------------------------------------------------------------------------
// SearchManager
// ---------------------------------------------------------------------------
SearchManager::SearchManager(QObject *parent) : QObject(parent)
{
    m_workerPool = new QThreadPool(this);
    m_workerPool->setObjectName("searchWorkerPool");
    m_workerPool->setMaxThreadCount(4);
    m_workerPool->setExpiryTimeout(30000);
    auto *worker = new SearchWorker(this);

    connect(worker, &SearchWorker::searchCompleted, this, [this](const QVariantList &r) {
        emit searchResultReady(r, QString());
    });

    m_tickTimer.setInterval(kTickMs);
    m_tickTimer.setObjectName("searchTaskTick");
    connect(&m_tickTimer, &QTimer::timeout, this, &SearchManager::tick);
}

SearchManager::~SearchManager()
{
    m_tickTimer.stop();
    mc_qt_download_set_cancel(1);
    for (const auto &st : m_taskStates) {
        if (st->thread && st->thread->isRunning())
            st->thread->wait(3000);
    }
    if (m_workerPool) {
        m_workerPool->waitForDone(3000);
        delete m_workerPool;
        m_workerPool = nullptr;
    }
}

bool SearchManager::busy() const
{
    for (const auto &task : m_tasks) {
        const QString s = task.toMap().value("status").toString();
        if (s == "queued" || s == "downloading" || s == "cancelling") return true;
    }
    return false;
}

QString SearchManager::installDir(const QString &rootDir, const QString &type) const
{
    if (type == "shader")       return QDir(rootDir).filePath("shaderpacks");
    if (type == "datapack")     return QDir(rootDir).filePath("datapacks");
    return QDir(rootDir).filePath("resourcepacks");
}

void SearchManager::search(const QString &query, const QString &sort, int limit,
                             const QString &mcVersion, const QString &loader,
                             int offset, const QString &source,
                             const QString &type)
{
    // Fire and forget: each call spawns its own thread. Results arrive via the
    // global searchCompleted signal; each SearchResourcePage filters them via
    // the type property it was created with (no mutual exclusion needed).

    // Reuse the thread pool worker for search tasks.
    auto *worker = new SearchWorker;
    QThread *wt = new QThread(this);
    worker->moveToThread(wt);
    connect(worker, &SearchWorker::searchCompleted, this, [this, worker](const QVariantList &r) {
        worker->deleteLater();
    }, Qt::QueuedConnection);
    connect(worker, &SearchWorker::searchCompleted, this, [this, type](const QVariantList &r) {
        emit searchResultReady(r, type);
    });
    connect(wt, &QThread::finished, wt, &QObject::deleteLater);
    connect(wt, &QThread::finished, worker, &QObject::deleteLater);
    wt->start();
    QMetaObject::invokeMethod(worker, "doSearch", Qt::QueuedConnection,
        Q_ARG(QString, query), Q_ARG(QString, sort), Q_ARG(int, limit),
        Q_ARG(QString, mcVersion), Q_ARG(QString, loader), Q_ARG(int, offset),
        Q_ARG(QString, source), Q_ARG(QString, type));
}

void SearchManager::getProject(const QString &, const QString &, const QString &)
{
    emit errorOccurred(QString());
}

void SearchManager::getFiles(const QString &projectId, const QString &type)
{
    McSearchFile out[100];
    for (int i = 0; i < 100; ++i) memset(&out[i], 0, sizeof(McSearchFile));

    int count = mc_search_get_files(
        projectId.toUtf8().constData(),
        nullptr,   // mc_version — not needed here
        nullptr,   // loader
        out, 100);

    QVariantList list;
    for (int i = 0; i < count; ++i) {
        QVariantMap m;
        m["id"]             = QString::fromUtf8(out[i].file_id);
        m["fileName"]       = QString::fromUtf8(out[i].file_name);
        m["downloadUrl"]    = QString::fromUtf8(out[i].download_url);
        m["sha1"]           = QString::fromUtf8(out[i].sha1);
        m["size"]           = (qlonglong)out[i].size;
        m["versionType"]    = QString::fromUtf8(out[i].version_type);
        m["datePublished"]  = QString::fromUtf8(out[i].date_published);
        m["isPrimary"]      = out[i].is_primary;
        list.append(m);
        mc_search_file_free(&out[i]);
    }

    emit filesLoaded(list);
}

void SearchManager::installResult(const QVariantMap &result, const QString &rootDir)
{
    QString fileName = result.value("fileName").toString();
    if (fileName.isEmpty()) fileName = result.value("name").toString();
    QString url = result.value("downloadUrl").toString();
    QString type = result.value("type").toString();
    if (url.isEmpty() || rootDir.isEmpty()) {
        emit errorOccurred(QString());
        return;
    }
    if (type.isEmpty()) type = "resourcepack";

    QString name = result.value("name").toString();
    if (name.isEmpty()) name = fileName;

    auto st = std::make_shared<SearchTaskState>();
    st->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st->name = name;
    st->fileName = fileName;
    st->url = url;
    st->sha1 = result.value("sha1").toString();
    st->rootDir = rootDir;
    st->type = type;
    st->expectedSize = result.value("size").toLongLong();

    QVariantMap t;
    t["id"] = st->id;
    t["name"] = name;
    t["fileName"] = fileName;
    t["type"] = type;
    t["status"] = "queued";
    t["progress"] = 0.0;
    t["received"] = (qlonglong)0;
    t["total"] = (qlonglong)0;
    t["speedBytes"] = (qlonglong)0;
    t["error"] = "";
    m_tasks.append(t);
    st->modelIndex = (int)m_tasks.size() - 1;
    m_taskStates.append(st);

    emit tasksChanged();
    pump();
}

void SearchManager::pump()
{
    int running = 0;
    for (const auto &st : m_taskStates)
        if (st->started.load()) running++;
    for (int i = 0; i < m_tasks.size() && running < kMaxConcurrent; ++i) {
        if (m_tasks[i].toMap().value("status").toString() != "queued") continue;
        auto st = m_taskStates[i];
        if (st->started.load()) continue;
        startTask(st);
        running++;
    }
    if (hasActive()) m_tickTimer.start();
}

void SearchManager::startTask(const std::shared_ptr<SearchTaskState> &st)
{
    st->started = 1;
    st->done = 0;
    st->cancelled = 0;
    st->received = 0;
    st->total = 0;
    st->lastSampleReceived = 0;
    st->speedBytes = 0.0;

    QVariantMap t = m_tasks[st->modelIndex].toMap();
    t["status"] = "downloading";
    t["progress"] = 0.0;
    t["received"] = (qlonglong)0;
    t["total"] = (qlonglong)0;
    t["speedBytes"] = (qlonglong)0;
    t["error"] = "";
    m_tasks[st->modelIndex] = t;
    emit tasksChanged();

    QString outPath = QDir(installDir(st->rootDir, st->type)).filePath(st->fileName);
    QDir().mkpath(QFileInfo(outPath).absolutePath());

    QThread *thread = QThread::create([st, url = st->url, outPath, sha1 = st->sha1,
                                        size = st->expectedSize]() {
        mc_qt_download_thread_no_pump(1);
        QByteArray urlBa = url.toUtf8();
        QByteArray outBa = outPath.toUtf8();
        QByteArray sha1Ba = sha1.toUtf8();

        int ok = mc_qt_download_file_progress(
            urlBa.constData(), outBa.constData(),
            sha1Ba.isEmpty() ? nullptr : sha1Ba.constData(),
            size, 30000,
            [](long long recv, long long total, void *ud) {
                auto *s = static_cast<SearchTaskState *>(ud);
                s->received = recv;
                s->total = total;
            }, st.get());

        st->done = ok ? 1 : 2;
        st->started = 0;
    });
    st->thread = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool SearchManager::hasActive() const
{
    for (const auto &task : m_tasks) {
        const QString s = task.toMap().value("status").toString();
        if (s == "queued" || s == "downloading" || s == "cancelling") return true;
    }
    return false;
}

void SearchManager::tick()
{
    bool changed = false;
    for (int i = 0; i < m_tasks.size(); ++i) {
        QVariantMap t = m_tasks[i].toMap();
        const QString status = t.value("status").toString();
        if (status != "downloading" && status != "cancelling") continue;

        auto st = m_taskStates[i];
        long long total = st->total.load();
        long long received = st->received.load();
        qreal prog = total > 0 ? qBound(0.0, (qreal)received / (qreal)total, 1.0) : 0.0;

        qreal speed = 0.0;
        long long delta = received - st->lastSampleReceived;
        st->lastSampleReceived = received;
        if (delta > 0) {
            qreal inst = (qreal)delta * (1000.0 / kTickMs);
            st->speedBytes = st->speedBytes > 0 ? (st->speedBytes * 0.7) + (inst * 0.3) : inst;
        } else if (received <= 0) {
            st->speedBytes = 0.0;
        }
        speed = st->speedBytes;

        if (t.value("progress").toDouble() != prog ||
            t.value("received").toLongLong() != received ||
            t.value("total").toLongLong() != total ||
            t.value("speedBytes").toLongLong() != (qlonglong)speed) {
            t["progress"] = prog;
            t["received"] = (qlonglong)received;
            t["total"] = (qlonglong)total;
            t["speedBytes"] = (qlonglong)speed;
            t["index"] = i;
            changed = true;
        }

        if (!st->started.load() && st->done.load() != 0) {
            if (st->cancelled.load()) {
                t["status"] = "cancelled";
                t["speedBytes"] = (qlonglong)0;
                QFile::remove(QDir(installDir(st->rootDir, st->type)).filePath(st->fileName));
                changed = true;
            } else if (st->done.load() == 1) {
                t["status"] = "success";
                t["progress"] = 1.0;
                t["received"] = (qlonglong)st->received.load();
                t["speedBytes"] = (qlonglong)0;
                changed = true;
                emit installCompleted(true,
                    QDir(installDir(st->rootDir, st->type)).filePath(st->fileName));
            } else {
                t["status"] = "failed";
                t["error"] = QString();
                t["speedBytes"] = (qlonglong)0;
                changed = true;
                emit installCompleted(false, QString());
            }
        }
        if (t != m_tasks[i]) m_tasks[i] = t;
    }
    if (changed) emit tasksChanged();
    pump();
    if (hasActive()) m_tickTimer.start();
    else m_tickTimer.stop();
}

void SearchManager::cancelTask(int index)
{
    if (index < 0 || index >= m_tasks.size()) return;
    QVariantMap t = m_tasks[index].toMap();
    const QString status = t.value("status").toString();
    if (status == "queued") {
        t["status"] = "cancelled";
        m_tasks[index] = t;
        emit tasksChanged();
    } else if (status == "downloading") {
        auto st = m_taskStates[index];
        st->cancelled = 1;
        t["status"] = "cancelling";
        m_tasks[index] = t;
        emit tasksChanged();
    }
}

void SearchManager::retryTask(int index)
{
    if (index < 0 || index >= m_tasks.size()) return;
    QVariantMap t = m_tasks[index].toMap();
    const QString status = t.value("status").toString();
    if (status != "failed" && status != "cancelled") return;

    auto st = m_taskStates[index];
    st->started = 0; st->done = 0; st->cancelled = 0;
    st->received = 0; st->total = 0;
    st->lastSampleReceived = 0; st->speedBytes = 0.0;

    t["status"] = "queued";
    t["progress"] = 0.0;
    t["received"] = (qlonglong)0;
    t["total"] = (qlonglong)0;
    t["speedBytes"] = (qlonglong)0;
    t["error"] = "";
    m_tasks[index] = t;
    emit tasksChanged();
    pump();
}

void SearchManager::clearFinished()
{
    QVariantList newTasks;
    QVector<std::shared_ptr<SearchTaskState>> newStates;
    for (int i = 0; i < m_tasks.size(); ++i) {
        const QString s = m_tasks[i].toMap().value("status").toString();
        if (s == "success" || s == "failed" || s == "cancelled") continue;
        auto st = m_taskStates[i];
        st->modelIndex = (int)newStates.size();
        newStates.append(st);
        newTasks.append(m_tasks[i]);
    }
    if (newTasks.size() != m_tasks.size()) {
        m_tasks = newTasks;
        m_taskStates = newStates;
        emit tasksChanged();
    }
}
