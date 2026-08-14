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
#include "ModpackManager.h"
#include "InstallManager.h"
#include "DownloadWorker.h"

#include <mc_log.h>
#include <mc_zip.h>
#include <mc_hash.h>
#include <mc_download_qt.h>
#include <mc_mod.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QDateTime>
#include <atomic>
#include <thread>

// ─── Helpers ───────────────────────────────────────────────────────────────

static QString sanitizeId(const QString &name)
{
    QString out;
    for (QChar c : name) {
        if (c.isLetterOrNumber() || c == '-' || c == '_')
            out += c;
        else if (!out.isEmpty() && !out.endsWith('-'))
            out += '-';
    }
    while (out.startsWith('-')) out = out.mid(1);
    if (out.isEmpty()) out = "modpack";
    return out.left(48);
}

static QString uniqueInstanceId(const QString &rootDir, QString base)
{
    QString candidate = base;
    int n = 2;
    while (QDir(QDir(rootDir).filePath("versions/" + candidate)).exists()) {
        candidate = base + "-" + QString::number(n);
        ++n;
    }
    return candidate;
}

static bool copyDirRecursively(const QString &src, const QString &dst)
{
    QDir s(src);
    if (!s.exists()) return true;
    QDir().mkpath(dst);
    const QFileInfoList entries = s.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        QString target = QDir(dst).filePath(fi.fileName());
        if (fi.isDir()) {
            if (!copyDirRecursively(fi.absoluteFilePath(), target)) return false;
        } else {
            if (QFile::exists(target)) QFile::remove(target);
            if (!QFile::copy(fi.absoluteFilePath(), target)) return false;
        }
    }
    return true;
}

static bool removeDirRecursively(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) return true;
    return dir.removeRecursively();
}

static bool downloadFile(const QString &url, const QString &dest,
                         const QString &sha1, qint64 expectedSize,
                         const std::function<void(qreal)> &subProgress)
{
    struct Ctx { std::function<void(qreal)> fn; qint64 last = -1; };
    Ctx ctx;
    ctx.fn = subProgress;

    if (QFileInfo::exists(dest) && QFileInfo(dest).size() > 0) {
        char existing[64];
        if (sha1.isEmpty() ||
            (mc_hash_file_sha1(dest.toUtf8().constData(), existing, sizeof(existing)) &&
             QString::fromUtf8(existing) == sha1))
            return true;
        QFile::remove(dest);
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());

    QByteArray urlBa = url.toUtf8();
    QByteArray mirrorBa;
    char translated[2048] = "";
    if (mc_mod_translate_download_url(urlBa.constData(), translated, sizeof(translated)))
        mirrorBa = QByteArray(translated);

    const char *urls[2];
    int urlCount = 1;
    urls[0] = urlBa.constData();
    if (!mirrorBa.isEmpty()) {
        urls[0] = mirrorBa.constData();
        urls[1] = urlBa.constData();
        urlCount = 2;
    }
    QByteArray sha1Ba;
    const char *sha1c = nullptr;
    if (!sha1.isEmpty()) { sha1Ba = sha1.toUtf8(); sha1c = sha1Ba.constData(); }

    auto cb = [](const char *, long long received, long long total, void *userdata) {
        auto *c = static_cast<Ctx *>(userdata);
        if (!c || !c->fn) return;
        long long base = total > 0 ? total : (received > 0 ? received * 2 : 0);
        if (base <= 0) return;
        qreal p = qBound<qreal>(0.0, (qreal)received / (qreal)base, 1.0);
        if (c->last < 0 || p - c->last > 0.005) { c->last = p; c->fn(p); }
    };

    QByteArray destBa = dest.toUtf8();
    return mc_qt_download_multi_progress(urls, urlCount, destBa.constData(),
                                         sha1c, (long)expectedSize, 120000, cb, &ctx) == 1;
}

// ─── Core installer (runs on worker thread) ────────────────────────────────

static QString installPack(const QString &mrpackPath, const QString &rootDir,
                           const QString &iconUrl, QString *errorOut,
                           const std::function<void(qreal, const QString &)> &progress)
{
    QString tmp = QDir(rootDir).filePath("versions/.mrpack_tmp");
    QString verDir;   // the pack's instance dir; only removed if we created it
    auto fail = [errorOut, &tmp, &verDir](const QString &msg) -> QString {
        mc_error("[Pack] ERROR: %s", msg.toUtf8().constData());
        if (errorOut) *errorOut = msg;
        // A failed install must not leave partial folders behind: leftover
        // extraction / instance dirs confuse later scans and force a retry to
        // pick a "-2"/"-3" duplicate instead of reusing the intended id.
        removeDirRecursively(tmp);
        if (!verDir.isEmpty())
            removeDirRecursively(verDir);
        return QString();
    };

    mc_info("[Pack] === START mrpack=%s root=%s", mrpackPath.toUtf8().constData(),
            rootDir.toUtf8().constData());

    removeDirRecursively(tmp);
    QDir().mkpath(tmp);

    progress(0.02, "解压整合包");
    if (!QFileInfo::exists(mrpackPath))
        return fail("整合包文件不存在: " + mrpackPath);
    if (mc_zip_extract(mrpackPath.toUtf8().constData(), tmp.toUtf8().constData()) != 1)
        return fail("整合包解压失败");

    QFile idxFile(tmp + "/modrinth.index.json");
    QJsonObject idx;
    if (idxFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(idxFile.readAll());
        if (doc.isObject()) idx = doc.object();
    }
    if (idx.isEmpty())
        return fail("整合包缺少 modrinth.index.json");

    QString packName = idx["name"].toString();
    if (packName.isEmpty()) packName = QFileInfo(mrpackPath).completeBaseName();

    QJsonObject deps = idx["dependencies"].toObject();
    QString mcVersion = deps["minecraft"].toString();
    if (mcVersion.isEmpty())
        return fail("整合包未包含 Minecraft 版本信息");

    QString loader, loaderVer;
    if (deps.contains("fabric-loader")) { loader = "fabric"; loaderVer = deps["fabric-loader"].toString(); }
    else if (deps.contains("quilt-loader")) { loader = "quilt"; loaderVer = deps["quilt-loader"].toString(); }
    else if (deps.contains("neoforge")) { loader = "neoforge"; loaderVer = deps["neoforge"].toString(); }
    else if (deps.contains("forge")) { loader = "forge"; loaderVer = deps["forge"].toString(); }

    QString loaderVerId;
    if (loader.isEmpty()) {
        // 纯原版整合包（仅依赖 minecraft）：跳过加载器安装，直接全量下载原版
        mc_info("[Pack] Vanilla-only modpack, target=%s", mcVersion.toUtf8().constData());
        loaderVerId = mcVersion;
    } else {
        progress(0.05, "安装加载器 " + loader + " " + mcVersion);
        {
            QString err;
            if (!installLoaderSync(mcVersion, loader, loaderVer, QString(), rootDir, &err, &loaderVerId,
                                   [&progress](qreal p, const QString &s) {
                                       progress(0.05 + 0.45 * p, "安装加载器 " + s);
                                   }))
                return fail("加载器安装失败: " + err);
        }
        if (loaderVerId.isEmpty())
            loaderVerId = mcVersion;
    }

    // Instance identity is independent of the base-download outcome, so decide
    // it up front and let the two download halves (Minecraft base + pack files)
    // run concurrently inside the global persistent download pool.
    QString instanceId = uniqueInstanceId(rootDir, sanitizeId(packName) + "-" + (loader.isEmpty() ? "vanilla" : loader));
    verDir = QDir(rootDir).filePath("versions/" + instanceId);
    QDir().mkpath(verDir);

    // Minimal instance JSON inheriting from the loader version
    {
        QJsonObject v;
        v["id"] = instanceId;
        v["inheritsFrom"] = loaderVerId;
        v["type"] = "release";
        v["mainClass"] = "";
        QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        v["time"] = now;
        v["releaseTime"] = now;
        QFile vf(verDir + "/" + instanceId + ".json");
        if (vf.open(QIODevice::WriteOnly))
            vf.write(QJsonDocument(v).toJson(QJsonDocument::Indented));
    }

    // Collect modpack files (client-required) into the isolated game dir
    struct PackFile {
        QVector<QByteArray> urlBufs;
        QVector<const char *> urls;
        QByteArray pathBuf, sha1Buf;
        long size = 0;
        int fileIndex = -1;
    };

    QJsonArray files = idx["files"].toArray();
    QVector<PackFile> packFiles;
    packFiles.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        QJsonObject f = files[i].toObject();
        QString envClient = f["env"].toObject()["client"].toString();
        if (envClient == "unsupported") continue;

        QString path = f["path"].toString();
        QJsonArray downloads = f["downloads"].toArray();
        QString url = downloads.isEmpty() ? QString() : downloads[0].toString();
        QString sha1 = f["hashes"].toObject()["sha1"].toString();
        qint64 size = (qint64)f["fileSize"].toDouble(0);
        if (url.isEmpty() || path.isEmpty()) continue;

        PackFile pf;
        pf.fileIndex = i;
        pf.pathBuf = QDir(verDir).filePath(path).toUtf8();
        QDir().mkpath(QFileInfo(QDir(verDir).filePath(path)).absolutePath());
        if (!sha1.isEmpty()) pf.sha1Buf = sha1.toUtf8();
        pf.size = (long)size;

        QByteArray orig = url.toUtf8();
        char translated[2048] = "";
        if (mc_mod_translate_download_url(orig.constData(), translated, sizeof(translated)) &&
            translated[0])
            pf.urlBufs.append(QByteArray(translated));
        pf.urlBufs.append(orig);
        for (const QByteArray &b : pf.urlBufs)
            pf.urls.append(b.constData());

        packFiles.append(pf);
    }
    mc_info("[Pack] %d files to download for %s", packFiles.size(), instanceId.toUtf8().constData());

    // ── Parallel: Minecraft base + pack files ─────────────────────────────
    // Both halves submit to the global persistent download pool, so they share
    // the same worker threads instead of draining the network serially.
    std::atomic<qreal> baseP{0.0};
    std::atomic<qreal> packP{packFiles.isEmpty() ? 1.0 : 0.0};
    std::atomic<bool> baseFailed{false};

    auto reportCombined = [&progress, &baseP, &packP](const QString &status) {
        qreal combined = qMin<qreal>(baseP.load(), packP.load());
        progress(0.50 + 0.35 * combined, status);
    };

    auto packWorker = std::thread([&]() {
        // This thread must not pump the Qt event loop while the worker thread
        // is doing the same inside DownloadWorker::run: two threads calling
        // QCoreApplication::processEvents concurrently is undefined behavior
        // and crashes. Block on the pool futures instead; the pool threads own
        // their event loops and perform the actual network work.
        mc_qt_download_thread_no_pump(1);
        const int total = packFiles.size();
        if (total == 0) {
            packP = 1.0;
            return;
        }

        // Submit EVERY pack file in one batch: the pool runs at most
        // download/threads files concurrently (the "max simultaneous download
        // tasks" setting), so all mods download together instead of in
        // serialized chunks. A slow file never holds back the others.
        QVector<McQtBatchItem> items(total);
        for (int i = 0; i < total; ++i) {
            PackFile &pf = packFiles[i];
            items[i].urls = pf.urls.constData();
            items[i].url_count = pf.urls.size();
            items[i].path = pf.pathBuf.constData();
            items[i].sha1 = pf.sha1Buf.isEmpty() ? nullptr : pf.sha1Buf.constData();
            items[i].size = pf.size;
        }
        QVector<int> res(total, 0);
        mc_qt_download_batch_ex(items.constData(), total, 60000, res.data());

        int doneCount = 0;
        bool cancelled = false;
        for (int i = 0; i < total; ++i) {
            if (res[i]) { ++doneCount; continue; }
            if (mc_qt_download_cancel()) { cancelled = true; break; }
            PackFile &pf = packFiles[i];
            bool rOk = false;
            for (int r = 0; r < 3 && !rOk && !cancelled; ++r) {
                if (mc_qt_download_cancel()) { cancelled = true; break; }
                QThread::msleep(200 + r * 300);
                rOk = mc_qt_download_file_multi(pf.urls.data(), pf.urls.size(),
                                                pf.pathBuf.constData(),
                                                pf.sha1Buf.isEmpty() ? nullptr : pf.sha1Buf.constData(),
                                                0, 120000);
            }
            if (!rOk)
                mc_error("[Pack] Failed to download %s", pf.pathBuf.constData());
            ++doneCount;
            packP = (qreal)doneCount / (qreal)total;
            reportCombined(QString("下载整合包文件 (%1/%2)").arg(doneCount).arg(total));
        }
        if (!cancelled) {
            packP = 1.0;
            reportCombined(QString("下载整合包文件 (%1/%2)").arg(total).arg(total));
        }
    });

    // Minecraft base download (jar / libraries incl. natives / assets / logging)
    progress(0.50, "下载原版游戏文件");
    QString baseErr;
    {
        DownloadWorker dw(loaderVerId, rootDir);
        bool ok = false;
        QObject::connect(&dw, &DownloadWorker::progressChanged,
                         [&baseP, &reportCombined](qreal p, const QString &t) {
                             baseP = p;
                             reportCombined("下载游戏 " + t);
                         });
        QObject::connect(&dw, &DownloadWorker::finished,
                         [&ok, &baseErr](bool s, const QString &e) { ok = s; baseErr = e; });
        dw.run();
        if (!ok) {
            baseFailed = true;
            mc_info("[Pack] Base download failed, stopping pack files early: %s",
                    baseErr.toUtf8().constData());
            // Wake any in-flight pack-file downloads so the pack worker can
            // stop promptly and join instead of draining the whole batch.
            mc_qt_download_set_cancel(1);
        }
    }
    packWorker.join();
    if (baseFailed.load())
        mc_qt_download_set_cancel(0);

    // A base failure aborts the whole install; the pack worker stops early.
    // Pack-file failures are non-fatal: missing optional files degrade the pack
    // but the instance is still created, mirroring PCL behavior.
    if (baseFailed.load())
        return fail("原版游戏文件下载失败" + (baseErr.isEmpty() ? QString() : ": " + baseErr));

    // Apply overrides into the game dir
    progress(0.88, "应用整合包覆盖文件");
    copyDirRecursively(tmp + "/overrides", verDir);
    copyDirRecursively(tmp + "/client-overrides", verDir);

    // Instance icon (from project cover when available)
    if (!iconUrl.isEmpty()) {
        progress(0.94, "下载整合包图标");
        downloadFile(iconUrl, verDir + "/icon.png", QString(), 0, [](qreal) {});
    }

    removeDirRecursively(tmp);

    mc_info("[Pack] === COMPLETE id=%s", instanceId.toUtf8().constData());
    progress(1.0, "整合包安装完成");
    return instanceId;
}

// ─── Worker (lives in worker thread) ───────────────────────────────────────

class ModpackWorker : public QObject
{
    Q_OBJECT

public:
    explicit ModpackWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void doInstallFile(const QString &filePath, const QString &rootDir)
    {
        // Never pump the shared Qt event loop from this worker thread while the
        // GUI thread runs its own (concurrent processEvents is UB and crashes
        // inside Qt6Core, see mc_download_qt.cpp). The download APIs block on
        // the global pool's futures; the pool threads own their event loops.
        mc_qt_download_thread_no_pump(1);
        QString error;
        QString id = installPack(filePath, rootDir, QString(), &error,
                                 [this](qreal p, const QString &s) {
                                     emit progressReported(p, s);
                                 });
        if (id.isEmpty())
            emit errorOccurred(error);
        else
            emit installCompleted(id);
    }

    void doInstallProject(const QVariantMap &file, const QString &rootDir)
    {
        mc_qt_download_thread_no_pump(1);
        QString fileName = file.value("fileName").toString();
        QString url = file.value("downloadUrl").toString();
        QString sha1 = file.value("sha1").toString();
        qint64 size = file.value("size").toLongLong();
        QString iconUrl = file.value("iconUrl").toString();

        mc_info("[Pack] doInstallProject: %s (%lldB)", fileName.toUtf8().constData(),
                (long long)size);
        emit progressReported(0.01, "下载整合包");
        QString tmpDir = QDir(rootDir).filePath("versions/.mrpack_dl");
        QDir().mkpath(tmpDir);
        QString mrpackPath = QDir(tmpDir).filePath(
            fileName.isEmpty() ? "modpack.mrpack" : fileName);

        // The mirror/CDN can stall a ranged download mid-flight, so retry a few
        // times (clearing stale .chunk.N pieces each round) before giving up:
        // a transient stall must not abort the whole install.
        bool ok = false;
        for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
            if (attempt > 1) {
                removeDirRecursively(tmpDir);
                QDir().mkpath(tmpDir);
                QThread::msleep(500);
            }
            emit progressReported(0.01, QString("下载整合包 (%1/3)").arg(attempt));
            // Map the real byte progress (0..1) of the mrpack download into the
            // 0.01..0.45 window of the overall install, so the status panel no
            // longer sits frozen at 1% for the whole (possibly minutes-long)
            // download.
            ok = downloadFile(url, mrpackPath, sha1, size,
                              [this](qreal p) {
                                  emit progressReported(0.01 + 0.44 * p, "下载整合包");
                              });
        }

        QString error;
        QString id;
        if (ok) {
            mc_info("[Pack] mrpack downloaded, starting install: %s",
                    mrpackPath.toUtf8().constData());
            // installPack reports 0..1; shift it into the 0.45..1.0 window so
            // the overall bar keeps rising after the download phase.
            id = installPack(mrpackPath, rootDir, iconUrl, &error,
                             [this](qreal p, const QString &s) {
                                 emit progressReported(0.45 + 0.55 * p, s);
                             });
        }
        else
            error = "整合包下载失败: " + url;

        if (id.isEmpty())
            emit errorOccurred(error);
        else
            emit installCompleted(id);

        // Remove the whole temp dir (not just the main file): a failed ranged
        // download leaves .chunk.N / .PCLDownloading partials that would defeat
        // QDir().rmdir and pollute the next install with stale pieces.
        removeDirRecursively(tmpDir);
    }

signals:
    void progressReported(qreal progress, const QString &status);
    void installCompleted(const QString &instanceId);
    void errorOccurred(const QString &message);
};

// ─── ModpackManager (lives on main thread) ─────────────────────────────────

ModpackManager::ModpackManager(QObject *parent) : QObject(parent) {}

ModpackManager::~ModpackManager()
{
    stopWorkerThread();
}

void ModpackManager::setBusy(bool b)
{
    m_busy = b;
    emit busyChanged();
}

// Cooperatively stop the running worker, then reap the thread. Download loops
// poll the global cancel flag every ~100ms, so setting it makes in-flight
// downloads bail out within a second or two. Never force-terminate here:
// killing a thread that is inside a download event loop / worker pool leaves
// in-flight QNetworkReplies and batch child threads touching freed state.
void ModpackManager::stopWorkerThread()
{
    if (!m_workerThread) {
        mc_qt_download_set_cancel(0);
        return;
    }
    if (!m_workerThread->isRunning()) {
        m_workerThread = nullptr;
        m_worker = nullptr;
        return;
    }
    m_workerThread->requestInterruption();
    mc_qt_download_set_cancel(1);
    m_workerThread->quit();
    // Cancellation makes every in-flight download bail within ~100ms (the
    // download loops poll the cancel flag), and the pack-file retry loop in
    // installPack() also breaks out as soon as the flag is set, so the worker
    // thread and the packWorker std::thread it joins stop within a couple of
    // seconds. Wait it out for up to 30s instead of detaching: abandoning a
    // still-running QThread child means ~ModpackManager destroys it while
    // running -> SIGSEGV/abort.
    for (int i = 0; i < 150; ++i) {
        if (m_workerThread->wait(200)) break;
    }
    if (m_workerThread->isRunning()) {
        mc_error("[Pack] worker still running after 30s of cooperative cancel; "
                 "abandoning it (never terminate: killing the thread would free "
                 "installPack()'s stack under its running pack-file std::thread "
                 "-> use-after-free)");
        m_workerThread->requestInterruption();
        // Unparent so ~ModpackManager cannot destroy a still-running QThread.
        m_workerThread->setParent(nullptr);
        // The worker will still finish on its own once the pool honours cancel;
        // its late installCompleted/errorOccurred are dropped because we reset
        // m_worker below, so clear busy ourselves to keep the manager usable.
        m_busy = false;
        emit busyChanged();
        m_workerThread = nullptr;
        m_worker = nullptr;
        return;
    }
    mc_qt_download_set_cancel(0);
    m_workerThread = nullptr;
    m_worker = nullptr;
}

void ModpackManager::cancelAll()
{
    stopWorkerThread();
    if (m_busy)
        setBusy(false);
}

void ModpackManager::startPackInstall(ModpackManager *self, ModpackWorker *worker,
                                      const std::function<void()> &start)
{
    if (self->busy()) {
        mc_info("[Pack] Already busy, ignoring");
        return;
    }
    self->m_busy = true;
    self->m_progress = 0.0;
    self->m_status = "开始安装整合包";
    emit self->busyChanged();
    emit self->progressChanged(0.0);
    emit self->statusChanged(self->m_status);

    worker->moveToThread(self->m_workerThread);
    QObject::connect(self->m_workerThread, &QThread::started, worker, start,
                     Qt::DirectConnection);
    QObject::connect(worker, &ModpackWorker::progressReported, self,
                     [self](qreal p, const QString &s) {
                         self->m_progress = p;
                         self->m_status = s;
                         emit self->progressChanged(p);
                         emit self->statusChanged(s);
                     }, Qt::QueuedConnection);
    QObject::connect(worker, &ModpackWorker::errorOccurred, self,
                     [self, worker](const QString &msg) {
                         if (self->m_worker != worker) return;
                         mc_error("[Pack] Worker error: %s", msg.toUtf8().constData());
                         self->m_busy = false;
                         emit self->busyChanged();
                         emit self->errorOccurred(msg);
                         if (self->m_workerThread)
                             self->m_workerThread->quit();
                     }, Qt::QueuedConnection);
    QObject::connect(worker, &ModpackWorker::installCompleted, self,
                     [self, worker](const QString &id) {
                         if (self->m_worker != worker) return;
                         mc_info("[Pack] Completed: %s", id.toUtf8().constData());
                         self->m_busy = false;
                         self->m_progress = 1.0;
                         self->m_status = "整合包安装完成";
                         emit self->busyChanged();
                         emit self->progressChanged(1.0);
                         emit self->statusChanged(self->m_status);
                         emit self->installCompleted(id);
                         if (self->m_workerThread)
                             self->m_workerThread->quit();
                     }, Qt::QueuedConnection);
    QThread *t = self->m_workerThread;
    QObject::connect(t, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    QObject::connect(t, &QThread::finished, self,
                     [self, t]() {
                         // Only reap this thread if it is still the current one,
                         // so a stale thread finishing late cannot clobber the
                         // pointers of a newer install.
                         if (self->m_workerThread == t) {
                             self->m_worker = nullptr;
                             self->m_workerThread = nullptr;
                         }
                     }, Qt::QueuedConnection);

    self->m_workerThread->start();
}

void ModpackManager::installFromFile(const QString &filePath, const QString &rootDir)
{
    if (busy()) { mc_info("[Pack] Already busy, ignoring"); return; }
    stopWorkerThread();
    m_worker = new ModpackWorker;
    m_workerThread = new QThread(this);
    startPackInstall(this, m_worker, [w = m_worker, filePath, rootDir]() {
        w->doInstallFile(filePath, rootDir);
    });
}

void ModpackManager::installFromProject(const QVariantMap &file, const QString &rootDir)
{
    if (busy()) { mc_info("[Pack] Already busy, ignoring"); return; }
    stopWorkerThread();
    m_worker = new ModpackWorker;
    m_workerThread = new QThread(this);
    startPackInstall(this, m_worker, [w = m_worker, file, rootDir]() {
        w->doInstallProject(file, rootDir);
    });
}

#include "ModpackManager.moc"
