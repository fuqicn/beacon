#include "JavaManager.h"
#include "DownloadManager.h"
#include <mc_log.h>
#include <mc_java.h>
#include <mc_java_dl.h>
#include <mc_download.h>
#include <mc_download_qt.h>
#include <mc_path.h>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <cstdlib>

JavaManager::JavaManager(QObject *parent) : QObject(parent) {}
JavaManager::~JavaManager() {}

void JavaManager::setRuntimeDir(const QString &dir)
{
    if (m_runtimeDir != dir) {
        m_runtimeDir = dir;
        QDir().mkpath(dir);
        emit runtimeDirChanged();
    }
}

void JavaManager::findJavaAsync()
{
    if (m_searching) return;
    m_searching = true;
    emit searchingChanged();

    QTimer::singleShot(0, this, [this]() { doFindJava(); });
}

void JavaManager::doFindJava()
{
    McJavaRuntime runtimes[128];
    int count = mc_java_find_all(runtimes, 128);
    mc_info("Found %d Java runtimes", count);

    QVariantList list;
    for (int i = 0; i < count; ++i) {
        QVariantMap rt;
        rt["path"] = QString::fromUtf8(runtimes[i].path);
        rt["majorVersion"] = runtimes[i].major_version;
        rt["version"] = QString::fromUtf8(runtimes[i].version);
        rt["vendor"] = QString::fromUtf8(runtimes[i].vendor);
        rt["is64bit"] = (bool)runtimes[i].is_64bit;
        rt["isJdk"] = (bool)runtimes[i].is_jdk;
        list.append(rt);
    }

    m_runtimes = list;
    m_searching = false;
    emit runtimesChanged();
    emit searchingChanged();
}

void JavaManager::findJava()
{
    findJavaAsync();
}

void JavaManager::addBundledJava()
{
    if (m_runtimeDir.isEmpty()) return;

    // Filter duplicates by path before appending
    QStringList existingPaths;
    for (const auto &e : m_runtimes)
        existingPaths << e.toMap().value("path").toString();

    bool changed = false;
    QDir rtDir(m_runtimeDir);
    QStringList subdirs = rtDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &sub : subdirs) {
        QString javaPath;
#ifdef Q_OS_WIN
        javaPath = rtDir.filePath(sub + "/bin/java.exe");
#else
        javaPath = rtDir.filePath(sub + "/bin/java");
#endif
        if (QFile::exists(javaPath) && !existingPaths.contains(javaPath)) {
            QVariantMap rt;
            rt["path"] = javaPath;
            rt["majorVersion"] = 0;
            rt["version"] = sub;
            rt["vendor"] = "Bundled";
            rt["is64bit"] = true;
            rt["isJdk"] = false;
            m_runtimes.append(rt);
            changed = true;
        }
    }

    if (changed)
        emit runtimesChanged();
}

QVariantMap JavaManager::findBest() const
{
    McJavaRuntime best;
    if (mc_java_find_best(&best)) {
        QVariantMap rt;
        rt["path"] = QString::fromUtf8(best.path);
        rt["majorVersion"] = best.major_version;
        rt["version"] = QString::fromUtf8(best.version);
        rt["vendor"] = QString::fromUtf8(best.vendor);
        return rt;
    }
    return QVariantMap();
}

void JavaManager::downloadJava(int majorVersion)
{
    if (m_runtimeDir.isEmpty()) {
        emit errorOccurred("Runtime directory not set");
        emit javaDownloadFailed(majorVersion);
        return;
    }

    m_searching = true;
    emit searchingChanged();

    QTimer::singleShot(0, this, [this, majorVersion]() {
        doDownloadJava(majorVersion);
    });
}

void JavaManager::doDownloadJava(int majorVersion)
{
    mc_info("Downloading Java %d...", majorVersion);

    McJavaFileList list;
    memset(&list, 0, sizeof(list));

    if (!mc_java_download_manifest(majorVersion, "bmclapi", &list)) {
        mc_info("BMCLAPI mirror failed, trying mojang...");
        if (!mc_java_download_manifest(majorVersion, "mojang", &list)) {
            mc_error("Failed to fetch Java %d manifest from both mirrors", majorVersion);
            m_searching = false;
            emit searchingChanged();
            emit javaDownloadFailed(majorVersion);
            return;
        }
    }

    mc_info("Java %d: %d files to download", majorVersion, list.count);

    QString javaDir = m_runtimeDir + QString("/java-%1").arg(majorVersion);
    QDir().mkpath(javaDir);

    // Build lists of files that need downloading (skip existing)
    std::vector<int> needDownload;
    needDownload.reserve(list.count);

    for (int i = 0; i < list.count; ++i) {
        auto &f = list.files[i];
        char fullPath[1024];
        mc_path_join(javaDir.toUtf8().constData(), f.path, fullPath, sizeof(fullPath));

        QFileInfo fi(QString::fromUtf8(fullPath));
        if (fi.exists() && fi.size() == f.size) {
            continue;
        }
        needDownload.push_back(i);
    }

    int skipped = list.count - (int)needDownload.size();
    if (skipped > 0)
        mc_info("Java %d: %d files already exist, %d to download", majorVersion, skipped, (int)needDownload.size());

    if (!needDownload.empty()) {
        // Build arrays for batch download
        int dlCount = (int)needDownload.size();
        std::vector<QByteArray> urlBa, pathBa, sha1Ba;
        std::vector<const char *> urls, paths, sha1s;
        std::vector<long> sizes;
        std::vector<int> results(dlCount, 0);
        urlBa.reserve(dlCount); pathBa.reserve(dlCount); sha1Ba.reserve(dlCount);
        urls.reserve(dlCount); paths.reserve(dlCount); sha1s.reserve(dlCount);
        sizes.reserve(dlCount);

        for (int idx : needDownload) {
            auto &f = list.files[idx];
            char fullPath[1024];
            mc_path_join(javaDir.toUtf8().constData(), f.path, fullPath, sizeof(fullPath));

            QDir().mkpath(QFileInfo(QString::fromUtf8(fullPath)).absolutePath());

            // Generate BMCLAPI mirror URL for batch download
            char mirrorBuf[2048];
            QByteArray mirrorUrl;
            if (mc_download_translate_mojang_url(f.url, mirrorBuf, sizeof(mirrorBuf), "bmclapi"))
                mirrorUrl = QByteArray(mirrorBuf);
            else
                mirrorUrl = QByteArray(f.url);

            // Store mirror URL, original URL is stored via f.url
            urlBa.push_back(mirrorUrl);
            pathBa.push_back(QByteArray(fullPath));
            sha1Ba.push_back(QByteArray(f.sha1));
            urls.push_back(urlBa.back().constData());
            paths.push_back(pathBa.back().constData());
            sha1s.push_back(sha1Ba.back().constData());
            sizes.push_back(f.size);
        }

        mc_info("Java %d: batch downloading %d files...", majorVersion, dlCount);

        mc_qt_download_batch(urls.data(), paths.data(), sha1s.data(),
                             sizes.data(), dlCount, 30000, results.data());

        int batchOk = 0;
        for (int i = 0; i < dlCount; ++i) batchOk += results[i];
        mc_info("Java %d: batch download %d/%d succeeded", majorVersion, batchOk, dlCount);

        std::vector<int> retryIndices;
        for (int i = 0; i < dlCount; ++i) {
            if (!results[i]) retryIndices.push_back(i);
        }

        // Retry failures individually with PCLCE pattern (multi-URL + delay)
        if (!retryIndices.empty()) {
            for (int ri : retryIndices) {
                int origIdx = needDownload[ri];
                auto &f = list.files[origIdx];
                char fullPath[1024];
                mc_path_join(javaDir.toUtf8().constData(), f.path, fullPath, sizeof(fullPath));

                mc_info("  Retrying: %s", f.path);
                // Build BMCLAPI mirror URL for retry fallback
                char mirrorBuf[2048];
                const char *origUrl = f.url;
                const char *mirrorUrl = origUrl;
                if (mc_download_translate_mojang_url(origUrl, mirrorBuf, sizeof(mirrorBuf), "bmclapi"))
                    mirrorUrl = mirrorBuf;

                const char *rUrls[2] = {mirrorUrl, origUrl};
                bool rOk = false;
                for (int r = 0; r < 3 && !rOk; ++r) {
                    QThread::msleep(200 + (std::rand() % (100 + r * 200)));
                    rOk = mc_qt_download_file_multi(rUrls, 2, fullPath, f.sha1[0] ? f.sha1 : nullptr, f.size, 20000);
                }
                if (rOk) batchOk++;
            }
        }

        mc_info("Java %d: final %d/%d succeeded", majorVersion, batchOk, dlCount);
    }

    int fileCount = list.count;
    mc_java_file_list_free(&list);

    int downloaded = fileCount - (int)needDownload.size();
    // Count how many from needDownload actually succeeded (re-count after retry)
    mc_info("Java %d: downloaded %d/%d files", majorVersion, downloaded, fileCount);

    // Find the java executable
    QString javaPath;
#ifdef Q_OS_WIN
    javaPath = javaDir + "/bin/java.exe";
#else
    javaPath = javaDir + "/bin/java";
#endif

    m_searching = false;
    emit searchingChanged();

    if (QFile::exists(javaPath)) {
        QVariantMap rt;
        rt["path"] = javaPath;
        rt["majorVersion"] = majorVersion;
        rt["version"] = QString("Java %1 (Bundled)").arg(majorVersion);
        rt["vendor"] = "Bundled";
        rt["is64bit"] = true;
        rt["isJdk"] = false;
        m_runtimes.append(rt);
        emit runtimesChanged();
        emit javaDownloaded(javaPath, majorVersion);
    } else {
        emit javaDownloadFailed(majorVersion);
    }
}
