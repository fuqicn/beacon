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
#include "ModManager.h"
#include <mc_log.h>
#include <mc_download_qt.h>
#include <cstring>
#include <vector>

#include <QThread>
#include <QThreadPool>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDirIterator>
#include <QDesktopServices>
#include <QUrl>
#include <QUuid>

static const int kMaxConcurrent = 4;
static const int kTickMs = 150;   // UI progress/speed polling interval

static int sortFromString(const QString &s)
{
    if (s == "downloads") return MC_MOD_SORT_DOWNLOADS;
    if (s == "follows") return MC_MOD_SORT_FOLLOWS;
    if (s == "newest") return MC_MOD_SORT_NEWEST;
    if (s == "updated") return MC_MOD_SORT_UPDATED;
    return MC_MOD_SORT_RELEVANCE;
}

static QVariantMap projectToVariant(const McModProject &p)
{
    QVariantMap m;
    m["id"] = QString::fromUtf8(p.id);
    m["slug"] = QString::fromUtf8(p.slug);
    m["name"] = QString::fromUtf8(p.name);
    m["description"] = QString::fromUtf8(p.description);
    m["logoUrl"] = QString::fromUtf8(p.logo_url);
    m["downloadCount"] = p.download_count;
    m["gameVersions"] = QString::fromUtf8(p.game_versions);
    m["loaders"] = QString::fromUtf8(p.loaders);
    m["projectType"] = QString::fromUtf8(p.project_type);
    m["websiteUrl"] = QString::fromUtf8(p.website_url);
    return m;
}

static QVariantList projectsToVariant(const std::vector<McModProject> &results, int count)
{
    QVariantList list;
    for (int i = 0; i < count; ++i) {
        list.append(projectToVariant(results[i]));
        mc_mod_project_free((McModProject *)&results[i]);
    }
    return list;
}

// ---------------------------------------------------------------------------
// Worker: runs blocking mc_mod_* API calls off the main thread.
// ---------------------------------------------------------------------------

class ModWorker : public QObject
{
    Q_OBJECT
public:
    explicit ModWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void doSearch(const QString &query, const QString &sort, int limit,
                  const QString &mcVersion, const QString &loader)
    {
        int maxResults = qBound(1, limit, 100);
        std::vector<McModProject> results(maxResults);
        for (auto &r : results) mc_mod_project_init(&r);

        int count = mc_mod_search(
            query.isEmpty() ? nullptr : query.toUtf8().constData(),
            mcVersion.isEmpty() ? nullptr : mcVersion.toUtf8().constData(),
            loader.isEmpty() ? nullptr : loader.toUtf8().constData(),
            MC_MOD_MODRINTH, maxResults, sortFromString(sort),
            results.data(), maxResults);

        emit searchCompleted(projectsToVariant(results, count));
    }

    void doSearchPacks(const QString &query, const QString &sort, int limit,
                       const QString &mcVersion, const QString &loader)
    {
        int maxResults = qBound(1, limit, 100);
        std::vector<McModProject> results(maxResults);
        for (auto &r : results) mc_mod_project_init(&r);

        int count = mc_mod_search_pack(
            query.isEmpty() ? nullptr : query.toUtf8().constData(),
            mcVersion.isEmpty() ? nullptr : mcVersion.toUtf8().constData(),
            loader.isEmpty() ? nullptr : loader.toUtf8().constData(),
            maxResults, sortFromString(sort),
            results.data(), maxResults);

        emit packSearchCompleted(projectsToVariant(results, count));
    }

    void doGetProject(const QString &projectId)
    {
        McModProject project;
        mc_mod_project_init(&project);

        if (!mc_mod_get_project(projectId.toUtf8().constData(), MC_MOD_MODRINTH, &project)) {
            emit failed(QString("项目不存在: %1").arg(projectId));
            return;
        }
        QVariantMap m = projectToVariant(project);
        mc_mod_project_free(&project);
        emit projectLoaded(m);
    }

    void doGetVersions(const QString &projectId, const QString &mcVersion, const QString &loader)
    {
        McModFile files[100];
        for (int i = 0; i < 100; ++i) mc_mod_file_init(&files[i]);

        int count = mc_mod_get_versions(
            projectId.toUtf8().constData(), MC_MOD_MODRINTH,
            mcVersion.isEmpty() ? nullptr : mcVersion.toUtf8().constData(),
            loader.isEmpty() ? nullptr : loader.toUtf8().constData(),
            files, 100);

        QVariantList list;
        for (int i = 0; i < count; ++i) {
            QVariantMap m;
            m["id"] = QString::fromUtf8(files[i].id);
            m["projectId"] = QString::fromUtf8(files[i].project_id);
            m["displayName"] = QString::fromUtf8(files[i].display_name);
            m["fileName"] = QString::fromUtf8(files[i].file_name);
            m["downloadUrl"] = QString::fromUtf8(files[i].download_url);
            m["sha1"] = QString::fromUtf8(files[i].sha1);
            m["size"] = (qlonglong)files[i].size;
m["releaseType"] = QString::fromUtf8(files[i].release_type);
            m["releaseDate"] = QString::fromUtf8(files[i].release_date);
            m["downloadCount"] = files[i].download_count;
            m["gameVersions"] = QString::fromUtf8(files[i].game_versions ? files[i].game_versions : "");

            QVariantList deps;
            for (int j = 0; j < files[i].dependency_count; ++j) {
                QVariantMap d;
                d["projectId"] = QString::fromUtf8(files[i].dependencies[j].project_id);
                d["versionId"] = QString::fromUtf8(files[i].dependencies[j].version_id);
                d["fileName"] = QString::fromUtf8(files[i].dependencies[j].file_name);
                d["dependencyType"] = QString::fromUtf8(files[i].dependencies[j].dependency_type);
                deps.append(d);
            }
            m["dependencies"] = deps;

            list.append(m);
            mc_mod_file_free(&files[i]);
        }

        emit versionsLoaded(list);
    }

    void doGetProjects(const QStringList &ids)
    {
        std::vector<const char *> cids;
        std::vector<QByteArray> bufs;
        bufs.reserve(ids.size());
        cids.reserve(ids.size());
        for (const auto &id : ids) {
            if (id.isEmpty()) continue;
            bufs.push_back(id.toUtf8());
            cids.push_back(bufs.back().constData());
        }
        if (cids.empty()) {
            emit projectsLoaded(QVariantList());
            return;
        }

        std::vector<McModProject> results(cids.size());
        for (auto &r : results) mc_mod_project_init(&r);

        int count = mc_mod_get_projects(cids.data(), (int)cids.size(),
                                        results.data(), (int)results.size());
        emit projectsLoaded(projectsToVariant(results, count));
    }

signals:
    void searchCompleted(const QVariantList &results);
    void packSearchCompleted(const QVariantList &results);
    void projectLoaded(const QVariantMap &project);
    void versionsLoaded(const QVariantList &versions);
    void projectsLoaded(const QVariantList &projects);
    void failed(const QString &message);
};

// ---------------------------------------------------------------------------
// ModManager: API calls + concurrent install queue.
// ---------------------------------------------------------------------------

ModManager::ModManager(QObject *parent) : QObject(parent)
{
    m_workerPool = new QThreadPool(this);
    m_workerPool->setObjectName("modWorkerPool");
    m_workerPool->setMaxThreadCount(4);
    m_workerPool->setExpiryTimeout(30000);
    m_worker = new ModWorker(this);

    connect(m_worker, &ModWorker::searchCompleted, this, [this](const QVariantList &r) {
        m_searching = false;
        emit searchingChanged();
        emit searchCompleted(r);
    });
    connect(m_worker, &ModWorker::packSearchCompleted, this, [this](const QVariantList &r) {
        m_searchingPacks = false;
        emit searchingPacksChanged();
        emit packSearchCompleted(r);
    });
    connect(m_worker, &ModWorker::projectLoaded, this, &ModManager::projectLoaded);
    connect(m_worker, &ModWorker::versionsLoaded, this, &ModManager::versionsLoaded);
    connect(m_worker, &ModWorker::projectsLoaded, this, &ModManager::projectsLoaded);
    connect(m_worker, &ModWorker::failed, this, [this](const QString &msg) {
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        if (m_searchingPacks) {
            m_searchingPacks = false;
            emit searchingPacksChanged();
        }
        emit errorOccurred(msg);
    });

    m_tickTimer.setInterval(kTickMs);
    m_tickTimer.setObjectName("modTaskTick");
    connect(&m_tickTimer, &QTimer::timeout, this, &ModManager::tick);
}

ModManager::~ModManager()
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

bool ModManager::busy() const
{
    for (const auto &t : m_tasks) {
        const QString s = t.toMap().value("status").toString();
        if (s == "queued" || s == "downloading" || s == "cancelling")
            return true;
    }
    return false;
}

QString ModManager::modsDir(const QString &rootDir) const
{
    return QDir(rootDir).filePath("mods");
}

void ModManager::search(const QString &query, const QString &sort, int limit,
                        const QString &mcVersion, const QString &loader)
{
    if (m_searching) return;
    m_searching = true;
    emit searchingChanged();

    m_workerPool->start([w = m_worker, query, sort, limit, mcVersion, loader]() {
        w->doSearch(query, sort, limit, mcVersion, loader);
    });
}

void ModManager::searchPacks(const QString &query, const QString &sort, int limit,
                            const QString &mcVersion, const QString &loader)
{
    if (m_searchingPacks) return;
    m_searchingPacks = true;
    emit searchingPacksChanged();

    m_workerPool->start([w = m_worker, query, sort, limit, mcVersion, loader]() {
        w->doSearchPacks(query, sort, limit, mcVersion, loader);
    });
}

void ModManager::getProject(const QString &projectId)
{
    m_workerPool->start([w = m_worker, projectId]() {
        w->doGetProject(projectId);
    });
}

void ModManager::getVersions(const QString &projectId,
                             const QString &mcVersion,
                             const QString &loader)
{
    m_workerPool->start([w = m_worker, projectId, mcVersion, loader]() {
        w->doGetVersions(projectId, mcVersion, loader);
    });
}

void ModManager::getProjects(const QStringList &ids)
{
    m_workerPool->start([w = m_worker, ids]() {
        w->doGetProjects(ids);
    });
}

QString ModManager::installMod(const QVariantMap &file, const QString &rootDir)
{
    QString fileName = file.value("fileName").toString();
    QString url = file.value("downloadUrl").toString();
    if (fileName.isEmpty() || url.isEmpty() || rootDir.isEmpty()) {
        emit errorOccurred("无效的模组文件");
        return QString();
    }

    QString name = file.value("displayName").toString();
    if (name.isEmpty()) name = fileName;

    auto st = std::make_shared<ModTaskState>();
    st->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st->name = name;
    st->fileName = fileName;
    st->url = url;
    st->sha1 = file.value("sha1").toString();
    st->rootDir = rootDir;
    st->expectedSize = file.value("size").toLongLong();

    QVariantMap t;
    t["id"] = st->id;
    t["name"] = name;
    t["fileName"] = fileName;
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
    return st->id;
}

void ModManager::pump()
{
    int running = 0;
    for (const auto &st : m_taskStates)
        if (st->started.load()) running++;

    for (int i = 0; i < m_tasks.size() && running < kMaxConcurrent; ++i) {
        if (m_tasks[i].toMap().value("status").toString() != "queued")
            continue;
        auto st = m_taskStates[i];
        if (st->started.load()) continue;
        startTask(st);
        running++;
    }
    if (hasActive())
        m_tickTimer.start();
}

void ModManager::startTask(const std::shared_ptr<ModTaskState> &st)
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

    char translated[2048] = "";
    QByteArray urlBa = st->url.toUtf8();
    bool hasMirror = mc_mod_translate_download_url(
        urlBa.constData(), translated, sizeof(translated));
    QString mirrorUrl = (hasMirror && translated[0]) ? QString::fromUtf8(translated) : QString();

    // Allow more parallel range pieces per file. May be overridden once at
    // startup; called here for safety so mod downloads benefit.
    if (mc_qt_download_max_pieces() < 8)
        mc_qt_download_set_max_pieces(8);

    QString outPath = QDir(modsDir(st->rootDir)).filePath(st->fileName);
    QDir().mkpath(QFileInfo(outPath).absolutePath());

    QThread *thread = QThread::create(
        [st, url = st->url, mirrorUrl, outPath, sha1 = st->sha1, size = st->expectedSize]() {
            QByteArray urlBa = url.toUtf8();
            QByteArray mirrorBa = mirrorUrl.toUtf8();
            QByteArray outBa = outPath.toUtf8();
            QByteArray sha1Ba = sha1.toUtf8();

            const char *urls[2];
            int urlCount = 1;
            urls[0] = urlBa.constData();
            if (!mirrorBa.isEmpty()) {
                urls[0] = mirrorBa.constData();
                urls[1] = urlBa.constData();
                urlCount = 2;
            }

            int ok = mc_qt_download_multi_progress(
                urls, urlCount, outBa.constData(),
                sha1Ba.isEmpty() ? nullptr : sha1Ba.constData(),
                size, 30000,
                [](const char *, long long recv, long long total, void *ud) {
                    auto *s = static_cast<ModTaskState *>(ud);
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

bool ModManager::hasActive() const
{
    for (const auto &t : m_tasks) {
        const QString s = t.toMap().value("status").toString();
        if (s == "queued" || s == "downloading" || s == "cancelling")
            return true;
    }
    return false;
}

void ModManager::tick()
{
    bool changed = false;
    for (int i = 0; i < m_tasks.size(); ++i) {
        QVariantMap t = m_tasks[i].toMap();
        const QString status = t.value("status").toString();
        if (status != "downloading" && status != "cancelling")
            continue;

        auto st = m_taskStates[i];

        long long total = st->total.load();
        long long received = st->received.load();
        qreal prog = t.value("progress").toDouble();
        if (total > 0) {
            prog = qBound(0.0, (qreal)received / (qreal)total, 1.0);
        }

        // Instant speed from the last sample, EWMA-smoothed to avoid jitter.
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
            changed = true;
        }

        if (!st->started.load() && st->done.load() != 0) {
            if (st->cancelled.load()) {
                t["status"] = "cancelled";
                t["speedBytes"] = (qlonglong)0;
                QFile::remove(QDir(modsDir(st->rootDir)).filePath(st->fileName));
                changed = true;
            } else if (st->done.load() == 1) {
                t["status"] = "success";
                t["progress"] = 1.0;
                t["received"] = (qlonglong)st->received.load();
                t["speedBytes"] = (qlonglong)0;
                changed = true;
                emit installCompleted(true,
                    QDir(modsDir(st->rootDir)).filePath(st->fileName));
            } else {
                t["status"] = "failed";
                t["error"] = "下载失败";
                t["speedBytes"] = (qlonglong)0;
                changed = true;
                emit installCompleted(false, QString());
            }
        }

        if (t != m_tasks[i])
            m_tasks[i] = t;
    }

    if (changed)
        emit tasksChanged();

    pump();

    if (hasActive())
        m_tickTimer.start();
    else
        m_tickTimer.stop();
}

void ModManager::cancelTask(int index)
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

void ModManager::retryTask(int index)
{
    if (index < 0 || index >= m_tasks.size()) return;
    QVariantMap t = m_tasks[index].toMap();
    const QString status = t.value("status").toString();
    if (status != "failed" && status != "cancelled")
        return;

            auto st = m_taskStates[index];
    st->started = 0;
    st->done = 0;
    st->cancelled = 0;
    st->received = 0;
    st->total = 0;
    st->lastSampleReceived = 0;
    st->speedBytes = 0.0;

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

void ModManager::clearFinished()
{
    QVariantList newTasks;
    QVector<std::shared_ptr<ModTaskState>> newStates;
    for (int i = 0; i < m_tasks.size(); ++i) {
        const QString s = m_tasks[i].toMap().value("status").toString();
        if (s == "success" || s == "failed" || s == "cancelled")
            continue;
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

QVariantList ModManager::listInstalledMods(const QString &rootDir) const
{
    QVariantList list;
    QDir dir(modsDir(rootDir));
    if (!dir.exists()) return list;

    QDirIterator it(dir.absolutePath(), QStringList() << "*.jar",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        QVariantMap m;
        m["fileName"] = fi.fileName();
        m["size"] = (qlonglong)fi.size();
        m["path"] = fi.absoluteFilePath();
        list.append(m);
    }
    return list;
}

bool ModManager::removeMod(const QString &rootDir, const QString &fileName)
{
    QString fullPath = QDir(modsDir(rootDir)).filePath(fileName);
    QFileInfo fi(fullPath);
    if (!fi.exists()) return false;
    return QFile::remove(fullPath);
}

void ModManager::openModsFolder(const QString &rootDir)
{
    QString modsDirPath = modsDir(rootDir);
    QDir().mkpath(modsDirPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(modsDirPath));
}

#include "ModManager.moc"
