#include "DownloadWorker.h"
#include <mc_log.h>
#include <mc_path.h>
#include <mc_http.h>
#include <mc_hash.h>
#include <mc_download.h>
#include <mc_download_qt.h>
#include <mc_library.h>
#include <mc_str.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// PCLCE-style download: try BMCLAPI first, then official URL, with up to 3 retry attempts and random delay
static bool mc_download_pclce(const char *url, const char *path, const char *sha1, long size, int timeout) {
    char mirrorUrl[2048];
    const char *urls[2];
    QByteArray mirrorBuf;
    if (mc_download_translate_mojang_url(url, mirrorUrl, sizeof(mirrorUrl), "bmclapi") && mirrorUrl[0]) {
        mirrorBuf = QByteArray(mirrorUrl);
        urls[0] = mirrorBuf.constData();
    } else {
        urls[0] = url;
    }
    urls[1] = url;
    bool ok = false;
    for (int r = 0; r < 3 && !ok; ++r) {
        QThread::msleep(200 + (std::rand() % (100 + r * 200)));
        ok = mc_qt_download_file_multi(urls, 2, path, sha1, size, timeout);
    }
    return ok;
}

DownloadWorker::DownloadWorker(const QString &versionId, const QString &dir)
    : QObject(nullptr), m_versionId(versionId), m_dir(dir)
{
}

DownloadWorker::~DownloadWorker() = default;

void DownloadWorker::setProgress(qreal p, const QString &task)
{
    m_progress = p;
    m_currentTask = task;
    emit progressChanged(p, task);
}

void DownloadWorker::setSubTask(const QString &t)
{
    m_subTask = t;
    emit subTaskChanged(t);
}

void DownloadWorker::finish(bool success)
{
    mc_info("[DL-W] finish: success=%d", success);
    emit finished(success, QString());
}

void DownloadWorker::translateUrl(const char *origUrl, char *out, size_t outSize)
{
    QString mirrorCopy = m_mirror;
    int ok = mc_download_translate_mojang_url(origUrl, out, outSize,
                                              mirrorCopy.toUtf8().constData());
    if (!ok || !out[0])
        qstrncpy(out, origUrl, outSize);
}

int DownloadWorker::batchDownloadWithRetry(QStringList &urls, QStringList &paths,
                                            QStringList &sha1s, int timeoutMs,
                                            QStringList *fallbackUrls)
{
    const int maxRetries = 3;
    const int chunkSize = 512;
    QStringList pendingUrls = urls;
    QStringList pendingPaths = paths;
    QStringList pendingSha1s = sha1s;
    QStringList pendingOrig = fallbackUrls ? *fallbackUrls : urls;

    m_totalFiles += pendingUrls.size();
    emit totalFilesChanged(m_totalFiles);
    m_lastEmit.start();

    mc_info("[DL-W]   batchDownloadWithRetry: maxRetries=%d chunkSize=%d total=%d",
            maxRetries, chunkSize, pendingUrls.size());
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        if (m_cancelled) return pendingUrls.size();
        int count = pendingUrls.size();
        if (count == 0) break;
        mc_info("[DL-W]   Attempt %d/%d: %d remaining", attempt, maxRetries, count);

        QStringList failUrls, failPaths, failSha1s, failOrig;
        int chunkStart = 0;
        while (chunkStart < count) {
            if (m_cancelled) return pendingUrls.size();
            int chunkEnd = std::min(chunkStart + chunkSize, count);
            int chunkCount = chunkEnd - chunkStart;

            setSubTask(attempt == 1
                ? QString("Downloading %1-%2/%3").arg(chunkStart + 1).arg(chunkEnd).arg(count)
                : QString("Retrying %1-%2/%3").arg(chunkStart + 1).arg(chunkEnd).arg(count));

            for (int i = chunkStart; i < chunkEnd; ++i) {
                char dir[1024];
                mc_path_dirname(pendingPaths[i].toUtf8().constData(), dir, sizeof(dir));
                mc_path_mkdir_p(dir);
            }

            std::vector<const char *> cUrls, cPaths, cSha1s;
            std::vector<QByteArray> urlBa, pathBa, sha1Ba;
            urlBa.reserve(chunkCount); pathBa.reserve(chunkCount); sha1Ba.reserve(chunkCount);
            cUrls.reserve(chunkCount); cPaths.reserve(chunkCount); cSha1s.reserve(chunkCount);

            for (int i = chunkStart; i < chunkEnd; ++i) {
                const QString &useUrl = (attempt == 1) ? pendingUrls[i] : pendingOrig[i];
                urlBa.push_back(useUrl.toUtf8());
                pathBa.push_back(pendingPaths[i].toUtf8());
                cUrls.push_back(urlBa[i - chunkStart].constData());
                cPaths.push_back(pathBa[i - chunkStart].constData());
                if (pendingSha1s[i].isEmpty()) {
                    sha1Ba.push_back(QByteArray());
                    cSha1s.push_back(nullptr);
                } else {
                    sha1Ba.push_back(pendingSha1s[i].toUtf8());
                    cSha1s.push_back(sha1Ba[i - chunkStart].constData());
                }
            }

            std::vector<long> sizes(chunkCount, 0);
            std::vector<int> results(chunkCount, 0);
            mc_info("[DL-W]   Chunk %d-%d/%d (tm=%d)...", chunkStart + 1, chunkEnd, count, timeoutMs);
            mc_qt_download_batch(cUrls.data(), cPaths.data(), cSha1s.data(),
                                 sizes.data(), chunkCount, timeoutMs, results.data());

            for (int i = chunkStart; i < chunkEnd; ++i) {
                if (results[i - chunkStart]) {
                    m_completedFiles++;
                } else {
                    failUrls << pendingUrls[i];
                    failPaths << pendingPaths[i];
                    failSha1s << pendingSha1s[i];
                    failOrig << pendingOrig[i];
                    m_totalFiles--;
                }
            }
            updateSpeed();
            chunkStart = chunkEnd;
        }

        if (failUrls.isEmpty()) break;
        mc_info("[DL-W]   Attempt %d: %d/%d ok, %d left", attempt, count - failUrls.size(), count, failUrls.size());

        pendingUrls = failUrls;
        pendingPaths = failPaths;
        pendingSha1s = failSha1s;
        pendingOrig = failOrig;
    }

    // Final flush
    emit completedFilesChanged(m_completedFiles);
    emit totalFilesChanged(m_totalFiles);
    return pendingUrls.size();
}

void DownloadWorker::emitProgress()
{
    qint64 elapsed = m_lastEmit.elapsed();
    if (elapsed < 80) return;
    m_lastEmit.restart();
    emit completedFilesChanged(m_completedFiles);
    emit totalFilesChanged(m_totalFiles);
}

void DownloadWorker::updateSpeed()
{
    qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed > 500) {
        int newBytes = m_completedFiles * 1024 * 100;
        m_speedBytes = (double)(newBytes - m_lastBytes) / (elapsed / 1000.0);
        m_lastBytes = newBytes;
        m_speedTimer.restart();
        emit speedBytesChanged(m_speedBytes);
        emitProgress();
    }
}

void DownloadWorker::run()
{
    mc_info("[DL-W] Worker started: versionId=%s dir=%s",
            m_versionId.toUtf8().constData(), m_dir.toUtf8().constData());

    m_totalFiles = 0;
    m_completedFiles = 0;
    m_speedBytes = 0.0;
    m_speedTimer.start();
    m_lastBytes = 0;

    mc_version_init(&m_ver);
    m_verOwned = false;

    mc_qt_dns_prefetch();

    doStartDownload();
}

void DownloadWorker::doStartDownload()
{
    if (m_cancelled) { finish(false); return; }

    m_attemptCount++;
    if (m_attemptCount <= MaxAttempts)
        m_mirror = "bmclapi";
    else {
        finish(false);
        return;
    }

    setProgress(0.0, QStringLiteral("Attempt %1/%2: %3")
                    .arg(m_attemptCount)
                    .arg(MaxAttempts)
                    .arg(m_versionId));

    stepFetchVersion();
}

void DownloadWorker::stepFetchVersion()
{
    qstrncpy(m_mcDir, m_dir.toUtf8().constData(), sizeof(m_mcDir));
    mc_info("[DL-W] Step 1: Fetch version JSON (mirror=%s)", m_mirror.toUtf8().constData());

    int ret = mc_version_fetch_by_id_mirror(
        &m_ver,
        m_versionId.toUtf8().constData(),
        m_mirror.toUtf8().constData());

    if (!ret) {
        mc_info("[DL-W] Step 1: Failed, will retry");
        retryOrFinish(false);
        return;
    }

    mc_path_join3(m_mcDir, "versions", m_ver.id, m_verDir, sizeof(m_verDir));
    mc_path_mkdir_p(m_verDir);

    char fn[256];
    snprintf(fn, sizeof(fn), "%s.json", m_ver.id);
    mc_path_join(m_verDir, fn, m_verJson, sizeof(m_verJson));
    snprintf(fn, sizeof(fn), "%s.jar", m_ver.id);
    mc_path_join(m_verDir, fn, m_verJar, sizeof(m_verJar));

    QFile f(QString::fromUtf8(m_verJson));
    if (f.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(m_ver.raw_json);
        f.write(doc.toJson());
    }

    m_verOwned = true;
    mc_info("[DL-W]   version=%s libs=%d", m_ver.id, m_ver.library_count);

    stepDownloadAll();
}

void DownloadWorker::stepDownloadAll()
{
    if (m_cancelled) { retryOrFinish(false); return; }

    setProgress(0.05, "Collecting files...");
    setSubTask("Scanning");

    QStringList dlPaths, dlUrls, dlOriginalUrls, dlSha1s;
    QList<long> dlSizes;
    int okCount = 0;
    QString libsDir = QDir(QString::fromUtf8(m_mcDir)).filePath("libraries");

    // ── 1. Client JAR ──────────────────────────────────────────
    if (m_ver.client_sha1[0]) {
        QFileInfo fi(QString::fromUtf8(m_verJar));
        bool needJar = !fi.exists();
        if (!needJar) {
            char existingSha1[64];
            if (mc_hash_file_sha1(m_verJar, existingSha1, sizeof(existingSha1)) &&
                mc_stricmp(existingSha1, m_ver.client_sha1) == 0) {
                okCount++;
            } else {
                needJar = true;
            }
        }
        if (needJar) {
            char url[2048];
            translateUrl(m_ver.client_url, url, sizeof(url));
            dlUrls << QString::fromUtf8(url);
            dlOriginalUrls << QString::fromUtf8(m_ver.client_url);
            dlPaths << QString::fromUtf8(m_verJar);
            dlSha1s << QString::fromUtf8(m_ver.client_sha1);
            dlSizes << 0;
        }
    }

    // ── 2. Libraries (self) ────────────────────────────────────
    for (int i = 0; i < m_ver.library_count; ++i) {
        auto &lib = m_ver.libraries[i];
        if (!lib.is_required) continue;

        if (lib.url[0]) {
            char libPath[1024];
            mc_library_resolve_path(lib.name, libPath, sizeof(libPath));
            QString fullPath = QDir(libsDir).filePath(QString::fromUtf8(libPath));

            QFileInfo fi(fullPath);
            bool need = !fi.exists();
            if (!need && lib.sha1[0]) {
                char existingSha1[64];
                if (mc_hash_file_sha1(fullPath.toUtf8().constData(), existingSha1, sizeof(existingSha1)))
                    need = (mc_stricmp(existingSha1, lib.sha1) != 0);
            }
            if (need) {
                char url[2048];
                QString origUrl = QString::fromUtf8(lib.url);
                translateUrl(lib.url, url, sizeof(url));
                dlPaths << fullPath;
                dlUrls << QString::fromUtf8(url);
                dlOriginalUrls << origUrl;
                dlSha1s << QString::fromUtf8(lib.sha1);
                dlSizes << lib.size;
            } else {
                okCount++;
            }
        }

        if (lib.is_natives) {
            char nativePath[1024], nativeUrl[2048];
            if (lib.classifier_url[0]) {
                mc_library_natives_path(lib.name, lib.natives_key, nativePath, sizeof(nativePath));
                QString fullNativePath = QDir(libsDir).filePath(QString::fromUtf8(nativePath));
                QFileInfo fi(fullNativePath);
                bool need = !fi.exists();
                if (!need && lib.classifier_sha1[0]) {
                    char existingSha1[64];
                    if (mc_hash_file_sha1(fullNativePath.toUtf8().constData(), existingSha1, sizeof(existingSha1)))
                        need = (mc_stricmp(existingSha1, lib.classifier_sha1) != 0);
                }
                if (need) {
                    translateUrl(lib.classifier_url, nativeUrl, sizeof(nativeUrl));
                    dlPaths << fullNativePath;
                    dlUrls << QString::fromUtf8(nativeUrl);
                    dlOriginalUrls << QString::fromUtf8(lib.classifier_url);
                    dlSha1s << QString::fromUtf8(lib.classifier_sha1);
                    dlSizes << 0;
                } else {
                    okCount++;
                }
            } else {
                mc_library_natives_path(lib.name, lib.natives_key, nativePath, sizeof(nativePath));
                QString fullNativePath = QDir(libsDir).filePath(QString::fromUtf8(nativePath));
                QFileInfo fi(fullNativePath);
                if (!fi.exists() && lib.url[0]) {
                    translateUrl(lib.url, nativeUrl, sizeof(nativeUrl));
                    char mirrorBase[512];
                    translateUrl("https://libraries.minecraft.net", mirrorBase, sizeof(mirrorBase));
                    char nativeUrlFinal[2048];
                    snprintf(nativeUrlFinal, sizeof(nativeUrlFinal), "%s/%s", mirrorBase, nativePath);
                    dlPaths << fullNativePath;
                    dlUrls << QString::fromUtf8(nativeUrlFinal);
                    dlOriginalUrls << QString("https://libraries.minecraft.net/%1").arg(QString::fromUtf8(nativePath));
                    dlSha1s << QString();
                    dlSizes << 0;
                } else {
                    okCount++;
                }
            }
        }
    }

    // ── 3. Libraries (inherits_from parent) ────────────────────
    if (m_ver.inherits_from[0]) {
        char parentJson[1024];
        mc_path_join3(m_mcDir, "versions", m_ver.inherits_from, parentJson, sizeof(parentJson));
        char pfn[256];
        snprintf(pfn, sizeof(pfn), "%s.json", m_ver.inherits_from);
        mc_path_join(parentJson, pfn, parentJson, sizeof(parentJson));

        if (!QFileInfo::exists(QString::fromUtf8(parentJson))) {
            char dirBuf[1024];
            mc_path_dirname(parentJson, dirBuf, sizeof(dirBuf));
            mc_path_mkdir_p(dirBuf);
            McVersion *parentVer = new McVersion;
            mc_version_init(parentVer);
            mc_version_fetch_by_id_mirror(parentVer, m_ver.inherits_from, m_mirror.toUtf8().constData());
            mc_version_free(parentVer);
            delete parentVer;
            if (!QFileInfo::exists(QString::fromUtf8(parentJson))) {
                mc_info("[DL-W] Parent version JSON not available: %s", m_ver.inherits_from);
            }
        }

        McVersion *parentVer = new McVersion;
        mc_version_init(parentVer);
        if (mc_version_parse_file(parentVer, parentJson)) {
            for (int i = 0; i < parentVer->library_count; ++i) {
                auto &plib = parentVer->libraries[i];
                if (!plib.is_required) continue;

                bool found = false;
                for (int j = 0; j < m_ver.library_count; ++j) {
                    if (strcmp(m_ver.libraries[j].name, plib.name) == 0) { found = true; break; }
                }
                if (found) continue;
                for (const auto &dp : dlPaths) {
                    if (dp.contains(QString::fromUtf8(plib.name))) { found = true; break; }
                }
                if (found) continue;

                if (plib.url[0]) {
                    char libPath[1024];
                    mc_library_resolve_path(plib.name, libPath, sizeof(libPath));
                    QString fullPath = QDir(libsDir).filePath(QString::fromUtf8(libPath));
                    QFileInfo fi(fullPath);
                    bool need = !fi.exists();
                    if (!need && plib.sha1[0]) {
                        char existingSha1[64];
                        if (mc_hash_file_sha1(fullPath.toUtf8().constData(), existingSha1, sizeof(existingSha1)))
                            need = (mc_stricmp(existingSha1, plib.sha1) != 0);
                    }
                    if (need) {
                        char url[2048];
                        QString origUrl = QString::fromUtf8(plib.url);
                        translateUrl(plib.url, url, sizeof(url));
                        dlPaths << fullPath;
                        dlUrls << QString::fromUtf8(url);
                        dlOriginalUrls << origUrl;
                        dlSha1s << QString::fromUtf8(plib.sha1);
                        dlSizes << plib.size;
                    } else {
                        okCount++;
                    }
                }

                if (plib.is_natives) {
                    char nativePath[1024], nativeUrl[2048];
                    if (plib.classifier_url[0]) {
                        mc_library_natives_path(plib.name, plib.natives_key, nativePath, sizeof(nativePath));
                        QString fullNativePath = QDir(libsDir).filePath(QString::fromUtf8(nativePath));
                        QFileInfo fi(fullNativePath);
                        bool need = !fi.exists();
                        if (!need && plib.classifier_sha1[0]) {
                            char existingSha1[64];
                            if (mc_hash_file_sha1(fullNativePath.toUtf8().constData(), existingSha1, sizeof(existingSha1)))
                                need = (mc_stricmp(existingSha1, plib.classifier_sha1) != 0);
                        }
                        if (need) {
                            translateUrl(plib.classifier_url, nativeUrl, sizeof(nativeUrl));
                            dlPaths << fullNativePath;
                            dlUrls << QString::fromUtf8(nativeUrl);
                            dlOriginalUrls << QString::fromUtf8(plib.classifier_url);
                            dlSha1s << QString::fromUtf8(plib.classifier_sha1);
                            dlSizes << 0;
                        } else {
                            okCount++;
                        }
                    } else {
                        mc_library_natives_path(plib.name, plib.natives_key, nativePath, sizeof(nativePath));
                        QString fullNativePath = QDir(libsDir).filePath(QString::fromUtf8(nativePath));
                        QFileInfo fi(fullNativePath);
                        if (!fi.exists() && plib.url[0]) {
                            translateUrl(plib.url, nativeUrl, sizeof(nativeUrl));
                            char mirrorBase[512];
                            translateUrl("https://libraries.minecraft.net", mirrorBase, sizeof(mirrorBase));
                            char nativeUrlFinal[2048];
                            snprintf(nativeUrlFinal, sizeof(nativeUrlFinal), "%s/%s", mirrorBase, nativePath);
                            dlPaths << fullNativePath;
                            dlUrls << QString::fromUtf8(nativeUrlFinal);
                            dlOriginalUrls << QString("https://libraries.minecraft.net/%1").arg(QString::fromUtf8(nativePath));
                            dlSha1s << QString();
                            dlSizes << 0;
                        } else {
                            okCount++;
                        }
                    }
                }
            }
            mc_version_free(parentVer);
            delete parentVer;
        }
    }

    m_totalFiles = okCount + dlPaths.size();
    m_completedFiles = okCount;
    emit totalFilesChanged(m_totalFiles);
    emit completedFilesChanged(m_completedFiles);

    // ── 4. Asset objects ───────────────────────────────────────
    setProgress(0.35, "Scanning assets...");
    McAssetIndex idx;
    memset(&idx, 0, sizeof(idx));
    if (mc_asset_index_fetch(&idx, &m_ver, m_mcDir)) {
        for (int i = 0; i < idx.count; ++i) {
            auto &obj = idx.objects[i];
            char objUrl[2048], objPath[1024];
            mc_asset_url(obj.hash, objUrl, sizeof(objUrl));
            mc_asset_object_path(obj.hash, m_mcDir, objPath, sizeof(objPath));

            QFileInfo fi(QString::fromUtf8(objPath));
            if (fi.exists() && fi.size() == obj.size) {
                m_totalFiles++;
                m_completedFiles++;
                continue;
            }

            dlUrls << QString::fromUtf8(objUrl);
            dlOriginalUrls << QString::fromUtf8(objUrl);
            dlPaths << QString::fromUtf8(objPath);
            dlSha1s << QString::fromUtf8(obj.hash);
            dlSizes << obj.size;
        }
        mc_asset_index_free(&idx);
    }
    m_totalFiles = okCount + dlPaths.size();
    emit totalFilesChanged(m_totalFiles);
    emit completedFilesChanged(m_completedFiles);

    mc_info("[DL-W] Total: %d files to download (JAR + libs + assets)", dlPaths.size());

    // ── 5. Download everything in one batch ────────────────────
    if (!dlPaths.isEmpty()) {
        setProgress(0.5, "Downloading libraries & assets...");
        batchDownloadWithRetry(dlUrls, dlPaths, dlSha1s, 30000, &dlOriginalUrls);
    }

    // ── 6. Logging config ──────────────────────────────────────
    stepDownloadLogging();
}

void DownloadWorker::stepDownloadLogging()
{
    mc_info("[DL-W] Step 5: enter");
    if (m_cancelled) { finish(false); return; }

    setProgress(0.9, "Downloading logging config...");
    setSubTask("Logging config");

    if (!m_ver.logging_client_url[0]) {
        mc_info("[DL-W] Step 5: No logging config, skipping");
        retryOrFinish(true);
        return;
    }

    const char *lastSlash = strrchr(m_ver.logging_client_url, '/');
    const char *filename = lastSlash ? lastSlash + 1 : "log4j2.xml";

    char logDir[1024], logPath[1024];
    mc_path_join3(m_mcDir, "assets", "logs", logDir, sizeof(logDir));
    mc_path_mkdir_p(logDir);
    mc_path_join(logDir, filename, logPath, sizeof(logPath));

    if (QFileInfo(QString::fromUtf8(logPath)).exists()) {
        mc_info("[DL-W] Step 5: Logging config already exists, skipping");
        retryOrFinish(true);
        return;
    }

    char url[2048];
    translateUrl(m_ver.logging_client_url, url, sizeof(url));
    mc_info("[DL-W] Step 5: Download logging config");

    int ok = mc_download_pclce(m_ver.logging_client_url, logPath, m_ver.logging_client_sha1, 0, 30000);
    mc_info("[DL-W]   result=%d", ok);

    if (ok) {
        m_completedFiles++;
        emit completedFilesChanged(m_completedFiles);
    }

    retryOrFinish(true);
}

void DownloadWorker::retryOrFinish(bool ok)
{
    mc_info("[DL-W] retryOrFinish: ok=%d attempt=%d/%d", ok, m_attemptCount, MaxAttempts);
    if (!ok && m_attemptCount < MaxAttempts) {
        if (m_verOwned) {
            mc_version_free(&m_ver);
            m_verOwned = false;
        }
        mc_version_init(&m_ver);
        doStartDownload();
    } else {
        if (m_verOwned) {
            mc_version_free(&m_ver);
            m_verOwned = false;
        }
        finish(ok);
    }
}
