#include "LaunchManager.h"
#include <mc_log.h>
#include <mc_path.h>
#include <mc_library.h>
#include <mc_hash.h>
#include <mc_str.h>
#include <mc_download.h>
#include <mc_download_qt.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTimer>
#include <QThread>
#include <cstdlib>
#include <windows.h>

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
#include <shellapi.h>
#include <zlib.h>

// Raw deflate decompression (ZIP storage method 8)
static bool rawInflate(const QByteArray &in, QByteArray &out, unsigned int expected) {
    z_stream strm; memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
    out.resize(expected ? expected : in.size() * 4);
    strm.next_in = (Bytef *)in.data();
    strm.avail_in = in.size();
    strm.next_out = (Bytef *)out.data();
    strm.avail_out = out.size();
    int ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) { inflateEnd(&strm); return false; }
    out.resize(strm.total_out);
    inflateEnd(&strm);
    return true;
}

// Shorten long paths using Win32 GetShortPathNameW (like PCLCE)
static QString shortenPath(const QString &path, int threshold = 247)
{
    if (path.length() <= threshold) return path;
    wchar_t shortBuf[1024];
    DWORD ret = GetShortPathNameW((LPCWSTR)path.utf16(), shortBuf, 1024);
    if (ret > 0 && ret < 1024)
        return QString::fromWCharArray(shortBuf);
    return path;
}

LaunchManager::LaunchManager(QObject *parent) : QObject(parent)
{
    mc_auth_init(&m_session);
}

LaunchManager::~LaunchManager()
{
    stop();
}

void LaunchManager::setVersionId(const QString &id)
{
    m_versionId = id;
    m_ready = !id.isEmpty() && !m_javaPath.isEmpty();
    emit versionChanged();
    emit readyChanged();
}

void LaunchManager::setJavaPath(const QString &path)
{
    m_javaPath = path;
    m_ready = !m_versionId.isEmpty() && !path.isEmpty();
    emit javaChanged();
    emit readyChanged();
}

void LaunchManager::setMcDir(const QString &dir)
{
    if (m_mcDir != dir) {
        m_mcDir = dir;
        emit mcDirChanged();
    }
}

void LaunchManager::setMemory(int mb)
{
    m_memory = mb;
    emit memoryChanged();
}

void LaunchManager::setSession(const McAuthSession *session)
{
    if (session) m_session = *session;
}

static QByteArray generateOfflineUuid(const QString &name)
{
    QString toHash = QString("OfflinePlayer:%1").arg(name);
    QByteArray hash = QCryptographicHash::hash(toHash.toUtf8(), QCryptographicHash::Md5);
    hash[6] = (hash[6] & 0x0f) | 0x30;
    hash[8] = (hash[8] & 0x3f) | 0x80;
    return hash.toHex();
}

// ============================================================
//  Async verification pipeline
// ============================================================

void LaunchManager::launch()
{
    mc_info("[Launch] launch() called: running=%d ready=%d javaPath=%s verId=%s",
            m_running, m_ready,
            m_javaPath.toUtf8().constData(),
            m_versionId.toUtf8().constData());
    if (m_running || !m_ready) {
        mc_info("[Launch] launch() SKIPPED: running=%d ready=%d", m_running, m_ready);
        return;
    }
    if (m_mcDir.isEmpty()) m_mcDir = QDir::homePath() + "/.minecraft";

    m_verifying = true;
    m_verifyProgress = 0.0;
    m_verifyTask = "正在验证文件...";
    emit verifyingChanged();
    emit verifyProgressChanged();

    QTimer::singleShot(0, this, [this]() { doVerifyAndLaunch(); });
}

void LaunchManager::doVerifyAndLaunch()
{
    mc_info("[Verify] doVerifyAndLaunch start: mcDir=%s verId=%s",
            m_mcDir.toUtf8().constData(), m_versionId.toUtf8().constData());

    // Parse version JSON
    mc_version_init(&m_vs.ver);
    m_vs.verLoaded = false;

    char verDir[1024], verJson[1024];
    mc_path_join3(m_mcDir.toUtf8().constData(), "versions",
                  m_versionId.toUtf8().constData(), verDir, sizeof(verDir));

    char fn[256];
    snprintf(fn, sizeof(fn), "%s.json", m_versionId.toUtf8().constData());
    mc_path_join(verDir, fn, verJson, sizeof(verJson));

    mc_info("[Verify] version JSON: %s", verJson);

    if (!mc_version_parse_file(&m_vs.ver, verJson)) {
        mc_error("[Verify] Failed to parse version JSON: %s", verJson);
        m_verifying = false;
        emit verifyingChanged();
        emit errorOccurred("版本 JSON 不存在: " + m_versionId);
        return;
    }
    m_vs.verLoaded = true;

    mc_info("[Verify] Parsed: mainClass=%s inherits_from=%s libs=%d id=%s jar=%s",
            m_vs.ver.main_class, m_vs.ver.inherits_from,
            m_vs.ver.library_count, m_vs.ver.id, m_vs.ver.jar);

    // Safety: cap library count
    if (m_vs.ver.library_count < 0 || m_vs.ver.library_count > MC_MAX_LIBRARIES) {
        mc_error("[Verify] library_count=%d out of range, capping to 0", m_vs.ver.library_count);
        m_vs.ver.library_count = 0;
    }

    // Handle inherits_from
    if (m_vs.ver.inherits_from[0]) {
        mc_info("[Verify] Handling inherits_from: %s", m_vs.ver.inherits_from);
        McVersion *parentVer = new McVersion;
        mc_version_init(parentVer);
        char parentJson[1024];
        mc_path_join3(m_mcDir.toUtf8().constData(), "versions",
                      m_vs.ver.inherits_from, parentJson, sizeof(parentJson));
        snprintf(fn, sizeof(fn), "%s.json", m_vs.ver.inherits_from);
        mc_path_join(parentJson, fn, parentJson, sizeof(parentJson));
        if (mc_version_parse_file(parentVer, parentJson)) {
            // Merge assets from parent if not set
            if (!m_vs.ver.assets[0] && parentVer->assets[0])
                qstrncpy(m_vs.ver.assets, parentVer->assets, sizeof(m_vs.ver.assets));
            if (!m_vs.ver.main_class[0] && parentVer->main_class[0])
                qstrncpy(m_vs.ver.main_class, parentVer->main_class, sizeof(m_vs.ver.main_class));
            // Merge asset_index from parent if not set
            if (!m_vs.ver.asset_index.id[0] && parentVer->asset_index.id[0]) {
                qstrncpy(m_vs.ver.asset_index.id, parentVer->asset_index.id, sizeof(m_vs.ver.asset_index.id));
                qstrncpy(m_vs.ver.asset_index.url, parentVer->asset_index.url, sizeof(m_vs.ver.asset_index.url));
                qstrncpy(m_vs.ver.asset_index.sha1, parentVer->asset_index.sha1, sizeof(m_vs.ver.asset_index.sha1));
                m_vs.ver.asset_index.size = parentVer->asset_index.size;
                m_vs.ver.asset_index.total_size = parentVer->asset_index.total_size;
            }
            // Merge client download info from parent if not set
            if (!m_vs.ver.client_url[0] && parentVer->client_url[0]) {
                qstrncpy(m_vs.ver.client_url, parentVer->client_url, sizeof(m_vs.ver.client_url));
                qstrncpy(m_vs.ver.client_sha1, parentVer->client_sha1, sizeof(m_vs.ver.client_sha1));
                m_vs.ver.client_size = parentVer->client_size;
            }
            // Merge jar field from parent if not set (e.g., Fabric/Quilt profiles inherit the client jar)
            if (!m_vs.ver.jar[0] && parentVer->jar[0]) {
                qstrncpy(m_vs.ver.jar, parentVer->jar, sizeof(m_vs.ver.jar));
            }
            // Merge libraries from parent
            for (int i = 0; i < parentVer->library_count && m_vs.ver.library_count < MC_MAX_LIBRARIES; ++i) {
                auto &plib = parentVer->libraries[i];
                bool found = false;
                for (int j = 0; j < m_vs.ver.library_count; ++j) {
                    if (strcmp(m_vs.ver.libraries[j].name, plib.name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    m_vs.ver.libraries[m_vs.ver.library_count++] = plib;
                }
            }
            mc_version_free(parentVer);
        }
        delete parentVer;
    }

    // Reset verify state
    m_vs.missingLibs.clear();
    m_vs.missingLibUrls.clear();
    m_vs.missingLibSha1s.clear();
    m_vs.missingAssets.clear();
    m_vs.missingAssetUrls.clear();
    m_vs.missingAssetSha1s.clear();
    m_vs.clientJarOk = false;
    m_vs.clientJarExists = false;
    m_vs.totalLibs = 0;
    m_vs.okLibs = 0;
    m_vs.totalAssets = 0;
    m_vs.okAssets = 0;

    verifyClientJar();
}

// Step 1: Check client JAR
void LaunchManager::verifyClientJar()
{
    m_verifyTask = "检查客户端...";
    m_verifyProgress = 0.05;
    emit verifyProgressChanged();

    char jarPath[1024];
    char fn[256];
    if (m_vs.ver.jar[0]) {
        mc_path_join3(m_mcDir.toUtf8().constData(), "versions", m_vs.ver.jar, jarPath, sizeof(jarPath));
        snprintf(fn, sizeof(fn), "%s.jar", m_vs.ver.jar);
        mc_path_join(jarPath, fn, jarPath, sizeof(jarPath));
    } else if (m_vs.ver.inherits_from[0]) {
        mc_path_join3(m_mcDir.toUtf8().constData(), "versions", m_vs.ver.inherits_from, jarPath, sizeof(jarPath));
        snprintf(fn, sizeof(fn), "%s.jar", m_vs.ver.inherits_from);
        mc_path_join(jarPath, fn, jarPath, sizeof(jarPath));
    } else {
        mc_path_join3(m_mcDir.toUtf8().constData(), "versions", m_vs.ver.id, jarPath, sizeof(jarPath));
        snprintf(fn, sizeof(fn), "%s.jar", m_vs.ver.id);
        mc_path_join(jarPath, fn, jarPath, sizeof(jarPath));
    }

    QFileInfo fi(QString::fromUtf8(jarPath));
    m_vs.clientJarExists = fi.exists();

    if (!m_vs.clientJarExists) {
        mc_info("[Verify] Client JAR missing: %s", jarPath);
        m_vs.clientJarOk = false;
    } else if (m_vs.ver.client_sha1[0]) {
        // Check SHA1
        char actualSha1[64];
        if (mc_hash_file_sha1(jarPath, actualSha1, sizeof(actualSha1))) {
            m_vs.clientJarOk = (mc_stricmp(actualSha1, m_vs.ver.client_sha1) == 0);
        } else {
            m_vs.clientJarOk = false;
        }
        if (!m_vs.clientJarOk)
            mc_info("[Verify] Client JAR corrupt: %s", jarPath);
    } else {
        // No SHA1 to verify, just check size > 1KB
        m_vs.clientJarOk = fi.size() > 1024;
    }

    QTimer::singleShot(0, this, [this]() { verifyLibraries(); });
}

// Step 2: Check libraries (existence + size + SHA1)
void LaunchManager::verifyLibraries()
{
    m_verifyTask = "检查支持库...";
    m_verifyProgress = 0.15;
    emit verifyProgressChanged();

    QString libsDir = QDir(m_mcDir).filePath("libraries");
    m_vs.totalLibs = m_vs.ver.library_count;

    for (int i = 0; i < m_vs.ver.library_count; ++i) {
        auto &lib = m_vs.ver.libraries[i];
        if (!lib.is_required) continue;

        char libPath[1024];
        mc_library_resolve_path(lib.name, libPath, sizeof(libPath));
        QString fullPath = QDir(libsDir).filePath(QString::fromUtf8(libPath));

        QFileInfo fi(fullPath);
        bool ok = fi.exists() && fi.size() > 0;
        if (ok && lib.sha1[0]) {
            char existing[64];
            if (mc_hash_file_sha1(fullPath.toUtf8().constData(), existing, sizeof(existing)) &&
                QString::fromUtf8(existing) != QString::fromUtf8(lib.sha1)) {
                ok = false;
                mc_info("[Verify] Library SHA1 mismatch: %s", lib.name);
            }
        } else if (ok && !lib.sha1[0]) {
            // No SHA1: verify it looks like a valid JAR, not an HTML error page
            QFile ft(fullPath);
            if (ft.open(QIODevice::ReadOnly)) {
                QByteArray head = ft.read(200);
                ft.close();
                if (fi.size() < 256 || head.size() < 2 ||
                    head[0] != 'P' || head[1] != 'K' ||
                    head.contains("<html") || head.contains("<!DOC")) {
                    ok = false;
                    mc_info("[Verify] Library corrupt (no SHA1, bad JAR): %s", lib.name);
                }
            }
        }
        if (ok) {
            m_vs.okLibs++;
        } else if (lib.url[0]) {
            m_vs.missingLibs << fullPath;
            m_vs.missingLibUrls << QString::fromUtf8(lib.url);
            m_vs.missingLibSha1s << QString::fromUtf8(lib.sha1);
            m_vs.missingLibSizes << lib.size;
        }
    }

    mc_info("[Verify] Libraries: %d/%d OK, %d missing",
            m_vs.okLibs, m_vs.totalLibs, m_vs.missingLibs.size());

    QTimer::singleShot(0, this, [this]() { verifyAssets(); });
}

// Step 3: Check assets (existence + size only - FAST)
void LaunchManager::verifyAssets()
{
    m_verifyTask = "检查资源文件...";
    m_verifyProgress = 0.4;
    emit verifyProgressChanged();

    // Parse asset index
    if (!m_vs.ver.asset_index.id[0]) {
        mc_info("[Verify] No asset index, skipping asset check");
        QTimer::singleShot(0, this, [this]() { downloadMissingFiles(); });
        return;
    }

    // Try to load asset index from disk
    char indexPath[1024];
    mc_path_join3(m_mcDir.toUtf8().constData(), "assets", "indexes", indexPath, sizeof(indexPath));
    char fn[256];
    snprintf(fn, sizeof(fn), "%s.json", m_vs.ver.asset_index.id);
    mc_path_join(indexPath, fn, indexPath, sizeof(indexPath));

    QFile indexFile(QString::fromUtf8(indexPath));
    if (!indexFile.open(QIODevice::ReadOnly)) {
        mc_info("[Verify] Asset index not found: %s", indexPath);
        // Will download asset index + assets later
        QTimer::singleShot(0, this, [this]() { downloadMissingFiles(); });
        return;
    }

    QJsonDocument indexDoc = QJsonDocument::fromJson(indexFile.readAll());
    QJsonObject indexRoot = indexDoc.object();
    QJsonObject objects = indexRoot["objects"].toObject();

    m_vs.totalAssets = objects.size();
    m_vs.okAssets = 0;

    char assetsDir[1024];
    mc_path_join(m_mcDir.toUtf8().constData(), "assets", assetsDir, sizeof(assetsDir));
    char objectsDir[1024];
    mc_path_join(assetsDir, "objects", objectsDir, sizeof(objectsDir));

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        QJsonObject obj = it.value().toObject();
        QJsonObject download = obj["hash"].toString().isEmpty()
                                   ? QJsonObject()
                                   : obj;

        QString hash = obj["hash"].toString();
        long size = (long)obj["size"].toDouble();

        if (hash.isEmpty()) continue;

        // Build object path: objects/<first2>/<hash>
        char objPath[1024];
        char hashDir[8] = {};
        qstrncpy(hashDir, hash.left(2).toUtf8().constData(), sizeof(hashDir));
        mc_path_join(objectsDir, hashDir, objPath, sizeof(objPath));
        mc_path_join(objPath, hash.toUtf8().constData(), objPath, sizeof(objPath));

        QFileInfo fi(QString::fromUtf8(objPath));
        if (fi.exists() && (size == 0 || fi.size() == size)) {
            m_vs.okAssets++;
        } else {
            // Missing or size mismatch - queue for download
            QString url = QString("https://resources.download.minecraft.net/%1/%2")
                              .arg(hash.left(2), hash);
            m_vs.missingAssets << QString::fromUtf8(objPath);
            m_vs.missingAssetUrls << url;
            m_vs.missingAssetSha1s << hash;
        }
    }

    mc_info("[Verify] Assets: %d/%d OK, %d missing",
            m_vs.okAssets, m_vs.totalAssets, m_vs.missingAssets.size());

    QTimer::singleShot(0, this, [this]() { downloadMissingFiles(); });
}

// Step 4: Download missing files (client JAR + libraries only, skip assets to avoid blocking)
void LaunchManager::downloadMissingFiles()
{
    // Only auto-download client JAR and libraries — assets should be pre-downloaded
    // Downloading 5000+ assets synchronously would freeze the UI for minutes
    int libsMissing = m_vs.missingLibs.size();
    bool clientMissing = !m_vs.clientJarOk;

    if (libsMissing == 0 && !clientMissing) {
        mc_info("[Verify] All critical files OK, proceeding to launch");
        QTimer::singleShot(0, this, [this]() { doLaunch(); });
        return;
    }

    // Cap: if too many libs missing (>500), something is wrong, skip auto-download
    if (libsMissing > 500) {
        mc_info("[Verify] Too many missing libs (%d), skipping auto-download", libsMissing);
        emit errorOccurred(QString("缺失 %1 个库文件，请先在下载页下载此版本").arg(libsMissing));
        QTimer::singleShot(0, this, [this]() { doLaunch(); });
        return;
    }

    int totalMissing = libsMissing + (clientMissing ? 1 : 0);
    m_verifyTask = QString("下载缺失文件 (%1)...").arg(totalMissing);
    m_verifyProgress = 0.5;
    emit verifyProgressChanged();

    mc_info("[Verify] %d files to download (client=%d, libs=%d)",
            totalMissing, clientMissing ? 1 : 0, libsMissing);

    // Build download lists
    QStringList allUrls, allPaths, allSha1s;
    std::vector<long> allSizes;

    // Client JAR (store both mirror and original for retry)
    QStringList allOrigUrls;
    if (clientMissing) {
        char jarUrl[2048];
        int ok = mc_download_translate_mojang_url(
            m_vs.ver.client_url, jarUrl, sizeof(jarUrl), "bmclapi");
        if (!ok || !jarUrl[0])
            qstrncpy(jarUrl, m_vs.ver.client_url, sizeof(jarUrl));

        char jarPath[1024];
        char fn[256];
        const char *jarVer = m_vs.ver.jar[0] ? m_vs.ver.jar
                          : (m_vs.ver.inherits_from[0] ? m_vs.ver.inherits_from
                          : m_vs.ver.id);
        mc_path_join3(m_mcDir.toUtf8().constData(), "versions", jarVer, jarPath, sizeof(jarPath));
        snprintf(fn, sizeof(fn), "%s.jar", jarVer);
        mc_path_join(jarPath, fn, jarPath, sizeof(jarPath));

        allUrls << QString::fromUtf8(jarUrl);
        allOrigUrls << QString::fromUtf8(m_vs.ver.client_url);
        allPaths << QString::fromUtf8(jarPath);
        allSha1s << QString::fromUtf8(m_vs.ver.client_sha1);
        allSizes.push_back(0);
    }

    // Libraries only (store both mirror and original for retry)
    for (int i = 0; i < m_vs.missingLibs.size(); ++i) {
        const QString &libUrl = m_vs.missingLibUrls[i];
        QString mirrorUrl;
        // Only translate Mojang URLs to BMCLAPI; Fabric/Forge Maven files don't exist on BMCLAPI
        bool isMojangUrl = libUrl.contains("piston-data.mojang.com") ||
                           libUrl.contains("piston-meta.mojang.com") ||
                           libUrl.contains("launcher.mojang.com") ||
                           libUrl.contains("launchermeta.mojang.com") ||
                           libUrl.contains("libraries.minecraft.net") ||
                           libUrl.contains("resources.download.minecraft.net");
        if (isMojangUrl) {
            char translated[2048];
            int ok = mc_download_translate_mojang_url(
                libUrl.toUtf8().constData(),
                translated, sizeof(translated), "bmclapi");
            if (ok && translated[0])
                mirrorUrl = QString::fromUtf8(translated);
            else
                mirrorUrl = libUrl;
        } else {
            mirrorUrl = libUrl;
        }

        allUrls << mirrorUrl;
        allOrigUrls << m_vs.missingLibUrls[i];
        allPaths << m_vs.missingLibs[i];
        allSha1s << m_vs.missingLibSha1s[i];
        allSizes.push_back(i < m_vs.missingLibSizes.size() ? m_vs.missingLibSizes[i] : 0);
    }

    // Ensure parent directories exist
    for (const auto &p : allPaths) {
        QDir().mkpath(QFileInfo(p).absolutePath());
    }

    // Download in batch
    int count = allUrls.size();
    std::vector<const char *> cUrls, cPaths, cSha1s, cOrig;
    std::vector<QByteArray> urlBa, pathBa, sha1Ba, origBa;
    std::vector<int> results(count, 0);
    urlBa.reserve(count); pathBa.reserve(count); sha1Ba.reserve(count); origBa.reserve(count);
    cUrls.reserve(count); cPaths.reserve(count); cSha1s.reserve(count); cOrig.reserve(count);
    for (int i = 0; i < count; ++i) {
        urlBa.push_back(allUrls[i].toUtf8());
        pathBa.push_back(allPaths[i].toUtf8());
        origBa.push_back(allOrigUrls[i].toUtf8());
        cUrls.push_back(urlBa[i].constData());
        cPaths.push_back(pathBa[i].constData());
        cOrig.push_back(origBa[i].constData());
        if (allSha1s[i].isEmpty()) {
            sha1Ba.push_back(QByteArray());
            cSha1s.push_back(nullptr);
        } else {
            sha1Ba.push_back(allSha1s[i].toUtf8());
            cSha1s.push_back(sha1Ba[i].constData());
        }
    }

    mc_qt_download_batch(cUrls.data(), cPaths.data(), cSha1s.data(),
                         allSizes.data(), count, 20000, results.data());

    int okCount = 0;
    for (int i = 0; i < count; ++i) okCount += results[i];
    mc_info("[Verify] Downloaded %d/%d files", okCount, count);

    // Post-verify: detect HTML error pages or non-ZIP files with no SHA1/size
    auto postVerify = [&](const char *phase) -> int {
        int n = 0;
        for (int i = 0; i < count; ++i) {
            if (!results[i]) continue;
            QString path = QString::fromUtf8(cPaths[i]);
            QFileInfo fi(path);
            if (!fi.exists() || fi.size() == 0) {
                mc_info("[Verify] %s: %s is missing or empty", phase, qUtf8Printable(path));
                QFile::remove(path); results[i] = 0; n++;
                continue;
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            QByteArray head = f.read(200);
            f.close();
            bool corrupt = false;
            if (head.contains("<html") || head.contains("<!DOC") || head.contains("<HTML"))
                corrupt = true;
            else if (fi.size() < 1000 && head.size() >= 2 && (head[0] != 'P' || head[1] != 'K'))
                corrupt = true;
            if (corrupt) {
                mc_info("[Verify] %s: %s is corrupt (size=%lld), removing",
                        phase, qUtf8Printable(path), fi.size());
                QFile::remove(path); results[i] = 0; n++;
            }
        }
        return n;
    };
    postVerify("Post-verify");

    // Retry failed files with PCLCE pattern
    okCount = 0;
    for (int i = 0; i < count; ++i) okCount += results[i];
    if (okCount < count) {
        mc_info("[Verify] Retrying %d failed downloads...", count - okCount);
        for (int i = 0; i < count; ++i) {
            if (!results[i]) {
                bool rOk = false;
                const char *rUrls[2] = {cOrig[i], cUrls[i]};
                for (int r = 0; r < 4 && !rOk; ++r) {
                    QThread::msleep(300 + (std::rand() % (200 + r * 300)));
                    rOk = mc_qt_download_file_multi(rUrls, 2, cPaths[i], cSha1s[i], 0, 120000);
                }
                mc_info("[Verify] Retry %s: %s", rOk ? "OK" : "FAIL", cPaths[i]);
            }
        }
        // Post-verify retried files too
        int bad = postVerify("Retry-verify");
        if (bad)
            mc_info("[Verify] Retry-verify caught %d still-corrupt files", bad);
    }

    m_verifyProgress = 0.9;
    m_verifyTask = "验证完成";
    emit verifyProgressChanged();

    // Re-check client JAR after download
    if (!m_vs.clientJarOk && !m_vs.ver.client_url[0]) {
        // Can't verify further without URL, just proceed
    }

    QTimer::singleShot(0, this, [this]() { doLaunch(); });
}

// ============================================================
//  Actual launch
// ============================================================

void LaunchManager::doLaunch()
{
    m_verifying = false;
    emit verifyingChanged();

    mc_info("[Launch] doLaunch: java=%s mcDir=%s verId=%s verLoaded=%d",
            m_javaPath.toUtf8().constData(),
            m_mcDir.toUtf8().constData(),
            m_versionId.toUtf8().constData(),
            m_vs.verLoaded);

    if (!m_vs.verLoaded) {
        emit errorOccurred("版本数据未加载");
        return;
    }

    if (m_javaPath.isEmpty()) {
        emit errorOccurred("Java 路径未设置");
        return;
    }

    if (!QFile::exists(m_javaPath)) {
        emit errorOccurred("Java 不存在: " + m_javaPath);
        return;
    }

    if (m_vs.ver.main_class[0] == '\0') {
        emit errorOccurred("版本 main_class 为空: " + m_versionId);
        return;
    }

    mc_info("[Launch] mainClass=%s assets=%s libs=%d",
            m_vs.ver.main_class, m_vs.ver.assets, m_vs.ver.library_count);

    if (m_vs.ver.library_count < 0 || m_vs.ver.library_count > MC_MAX_LIBRARIES) {
        mc_error("[Launch] Invalid library_count: %d, capping to 0", m_vs.ver.library_count);
        m_vs.ver.library_count = 0;
    }

    char mcDir[1024];
    qstrncpy(mcDir, m_mcDir.toUtf8().constData(), sizeof(mcDir));

    char fn[256];

    // Build classpath
    QStringList classpath;

    // Client JAR
    char jarPath[1024];
    const char *jarVer = m_vs.ver.jar[0] ? m_vs.ver.jar
                      : (m_vs.ver.inherits_from[0] ? m_vs.ver.inherits_from
                      : m_vs.ver.id);
    mc_path_join3(mcDir, "versions", jarVer, jarPath, sizeof(jarPath));
    snprintf(fn, sizeof(fn), "%s.jar", jarVer);
    mc_path_join(jarPath, fn, jarPath, sizeof(jarPath));
    classpath << QString::fromUtf8(jarPath);

    // Libraries
    QString libsDir = QDir(m_mcDir).filePath("libraries");
    for (int i = 0; i < m_vs.ver.library_count; ++i) {
        auto &lib = m_vs.ver.libraries[i];
        if (!lib.is_required) continue;
        char libPath[1024];
        mc_library_resolve_path(lib.name, libPath, sizeof(libPath));
        QString fullPath = QDir(libsDir).filePath(QString::fromUtf8(libPath));
        if (QFile::exists(fullPath))
            classpath << fullPath;
    }
    // JVM arguments
    QStringList jvmArgs;
    if (m_memory > 0) {
        jvmArgs << QString("-Xmx%1m").arg(m_memory) << QString("-Xms%1m").arg(m_memory);
    }

    if (!m_extraJvmArgs.isEmpty()) {
        QStringList extra = m_extraJvmArgs.split(' ', Qt::SkipEmptyParts);
        jvmArgs << extra;
    }

    // Game variable setup (needed by substPlaceholders)
    QString username = m_session.is_authenticated ? QString::fromUtf8(m_session.name) : m_username;
    QString uuid;
    QString accessToken;
    if (m_session.is_authenticated) {
        uuid = QString::fromUtf8(m_session.uuid);
        accessToken = QString::fromUtf8(m_session.access_token);
    } else {
        uuid = generateOfflineUuid(username);
        accessToken = "offline";
    }

    char assetsDir[1024];
    mc_path_join(mcDir, "assets", assetsDir, sizeof(assetsDir));

    // Natives dir (needed by substPlaceholders)
    char nativesDir[1024];
    if (!mc_path_join3(mcDir, "versions", m_vs.ver.id, nativesDir, sizeof(nativesDir))) {
        mc_error("[Launch] nativesDir path too long");
        qstrncpy(nativesDir, mcDir, sizeof(nativesDir));
    }
    if (!mc_path_join3(nativesDir, m_vs.ver.id, "-natives", nativesDir, sizeof(nativesDir))) {
        mc_error("[Launch] nativesDir path too long (2)");
    }
    mc_path_mkdir_p(nativesDir);

    // Extract native DLLs from native classifier JARs into natives directory
    QString libsDir2 = QDir(m_mcDir).filePath("libraries");
    for (int i = 0; i < m_vs.ver.library_count; ++i) {
        auto &lib = m_vs.ver.libraries[i];
        if (!lib.is_required) continue;
        if (!lib.is_natives && !lib.classifier_url[0]) continue;

        // Determine the native key for Windows
        const char *nativeKey = lib.natives_key;
        if (!nativeKey[0]) continue;
        QString key = QString::fromUtf8(nativeKey);
        if (key.contains("${arch}"))
            key.replace("${arch}", QSysInfo::currentCpuArchitecture().contains("64") ? "64" : "32");

        char nativePath[1024];
        mc_library_natives_path(lib.name, key.toUtf8().constData(), nativePath, sizeof(nativePath));
        if (!nativePath[0]) continue;

        QString jarPath = QDir(libsDir2).filePath(QString::fromUtf8(nativePath));
        if (!QFileInfo::exists(jarPath)) continue;

        // Open JAR as ZIP and extract .dll files
        QFile f(jarPath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray data = f.readAll();
        f.close();

        // Parse ZIP central directory and extract entries ending in .dll
        unsigned int eocdOffset = 0;
        int searchEnd = data.size() - 22;
        int searchStart = searchEnd - 65536;
        if (searchStart < 0) searchStart = 0;
        for (int pos = searchEnd; pos >= searchStart; pos--) {
            if ((unsigned char)data[pos] == 0x50 && (unsigned char)data[pos+1] == 0x4b &&
                (unsigned char)data[pos+2] == 0x05 && (unsigned char)data[pos+3] == 0x06) {
                eocdOffset = pos;
                break;
            }
        }
        if (eocdOffset == 0) continue;

        unsigned int cdOffset = *(unsigned int *)(data.data() + eocdOffset + 16);
        unsigned int cdEntries = *(unsigned short *)(data.data() + eocdOffset + 10);
        unsigned int pos2 = cdOffset;
        for (unsigned int e = 0; e < cdEntries; ++e) {
            if (pos2 + 46 > (unsigned)data.size()) break;
            unsigned int nameLen = *(unsigned short *)(data.data() + pos2 + 28);
            unsigned int extraLen = *(unsigned short *)(data.data() + pos2 + 30);
            unsigned int commentLen = *(unsigned short *)(data.data() + pos2 + 32);
            unsigned int localOffset = *(unsigned int *)(data.data() + pos2 + 42);
            unsigned int compSz = *(unsigned int *)(data.data() + pos2 + 20);
            unsigned int ucSz = *(unsigned int *)(data.data() + pos2 + 24);
            unsigned short method = *(unsigned short *)(data.data() + pos2 + 10);

            if (pos2 + 46 + nameLen > (unsigned)data.size()) break;
            QString entryName = QString::fromUtf8(data.mid(pos2 + 46, nameLen));
            pos2 += 46 + nameLen + extraLen + commentLen;

            if (!entryName.endsWith(".dll", Qt::CaseInsensitive)) continue;

            // Extract
            QByteArray raw;
            if (localOffset + 30 + nameLen > (unsigned)data.size()) continue;
            unsigned int lhExtraLen = *(unsigned short *)(data.data() + localOffset + 28);
            unsigned int fileStart = localOffset + 30 + nameLen + lhExtraLen;

            if (method == 0) {
                raw = data.mid(fileStart, ucSz);
            } else if (method == 8) {
                QByteArray compressed = data.mid(fileStart, compSz);
                if (!rawInflate(compressed, raw, ucSz)) continue;
            } else continue;

            QString outPath = QDir(QString::fromUtf8(nativesDir)).filePath(entryName);
            QFile of(outPath);
            if (of.open(QIODevice::WriteOnly)) {
                of.write(raw);
                mc_info("[Launch] Extracted native: %s", outPath.toUtf8().constData());
            }
        }
    }

    QString nativesDirStr = shortenPath(QString::fromUtf8(nativesDir));

    QJsonObject root = m_vs.ver.raw_json;

    auto substPlaceholders = [&](QString s) -> QString {
        s.replace("${auth_player_name}", username);
        s.replace("${auth_uuid}", uuid);
        s.replace("${auth_access_token}", accessToken);
        s.replace("${user_type}", m_session.is_authenticated ? QString::fromUtf8(m_session.user_type) : "offline");
        s.replace("${version_name}", m_versionId);
        s.replace("${version_type}", QString::fromUtf8(m_vs.ver.type));
        s.replace("${assets_root}", QString::fromUtf8(assetsDir));
        s.replace("${game_directory}", m_mcDir);
        s.replace("${assets_index_name}", QString::fromUtf8(m_vs.ver.assets));
        s.replace("${natives_directory}", nativesDirStr);
        s.replace("${user_properties}", "{}");
        QString session = QString("token:%1:%2").arg(accessToken, uuid);
        s.replace("${auth_session}", session);
        s.replace("${game_assets}", QString::fromUtf8(assetsDir));
        s.replace("${classpath}", classpath.join(';'));
        s.replace("${library_directory}", QDir(m_mcDir).filePath("libraries"));
        s.replace("${classpath_separator}", ";");
        s.replace("${launcher_name}", "beacon");
        s.replace("${launcher_version}", "1.0.0");
        return s;
    };

    // Parse arguments.jvm from version JSON with rules evaluation
    // Launcher feature flags for rules evaluation
    struct { const char *key; bool val; } features[] = {
        {"is_demo_user", false},
        {"has_custom_resolution", false},
        {"has_quick_plays_support", false},
        {"is_quick_play_singleplayer", false},
        {"is_quick_play_multiplayer", false},
        {"is_quick_play_realms", false},
    };

    auto featureEnabled = [&](const QString &key) -> bool {
        for (auto &f : features)
            if (key == QLatin1String(f.key)) return f.val;
        return false;
    };

    auto evalRules = [&](const QJsonArray &rules) -> bool {
        if (rules.isEmpty()) return true;
        bool allowed = false;
        for (const auto &r : rules) {
            auto rule = r.toObject();
            QString action = rule["action"].toString();
            if (action.isEmpty()) continue;

            bool matches = true;
            if (rule.contains("os")) {
                auto os = rule["os"].toObject();
                if (os.contains("name"))
                    matches = (os["name"].toString().compare("windows", Qt::CaseInsensitive) == 0);
                if (matches && os.contains("arch"))
                    matches = (os["arch"].toString().compare("x86", Qt::CaseInsensitive) == 0)
#if defined(_M_AMD64) || defined(__x86_64__)
                              ? false : true;
#else
                              ? true : false;
#endif
                if (matches && os.contains("version")) {
                    QRegularExpression re(os["version"].toString());
                    matches = re.match(QSysInfo::productVersion()).hasMatch();
                }
            }
            if (matches && rule.contains("features")) {
                auto feat = rule["features"].toObject();
                for (auto it = feat.begin(); it != feat.end(); ++it) {
                    if (it.value().isBool() && it.value().toBool() != featureEnabled(it.key())) {
                        matches = false;
                        break;
                    }
                }
            }

            if (matches) {
                if (action == "allow") allowed = true;
                else if (action == "disallow") allowed = false;
            }
        }
        return allowed;
    };

    // Build classpath string (needed by substPlaceholders and legacyClassPath.file)
    QStringList shortClasspath;
    for (auto &cp : classpath)
        shortClasspath << shortenPath(cp);
    QString classpathStr = shortClasspath.join(';');

    // Parse arguments.jvm from version JSON first (before building final args)
    // to detect module path (-p) and filter incompatible flags
    QStringList verJvmArgs;
    bool useModulePath = false;
    int javaMajor = 0;
    // Try to detect Java major version for flag filtering
    {
        QProcess jv;
        jv.setProgram(m_javaPath);
        jv.setArguments({"-version"});
        jv.setProcessChannelMode(QProcess::MergedChannels);
        jv.start();
        if (jv.waitForFinished(5000)) {
            QString out = QString::fromUtf8(jv.readAll());
            QRegularExpression re("\"(\\d+)\\.\\d+\\.\\d+");
            auto m = re.match(out);
            if (m.hasMatch()) {
                javaMajor = m.captured(1).toInt();
                if (javaMajor == 1) {
                    QRegularExpression re2("\"1\\.(\\d+)");
                    auto m2 = re2.match(out);
                    if (m2.hasMatch()) javaMajor = m2.captured(1).toInt();
                }
            }
        }
    }

    if (root.contains("arguments") && root["arguments"].isObject()) {
        auto argsObj = root["arguments"].toObject();
        if (argsObj.contains("jvm") && argsObj["jvm"].isArray()) {
            auto jvm = argsObj["jvm"].toArray();
            for (const auto &arg : jvm) {
                if (arg.isString()) {
                    QString s = substPlaceholders(arg.toString());
                    if (javaMajor > 0 && javaMajor < 22) {
                        if (s.startsWith("--sun-misc-unsafe-memory-access=") ||
                            s.startsWith("--enable-native-access="))
                            continue;
                    }
                    if (s == "-p") useModulePath = true;
                    verJvmArgs << s;
                } else if (arg.isObject()) {
                    auto obj = arg.toObject();
                    QJsonArray rules = obj["rules"].toArray();
                    QJsonValue val = obj["value"];
                    if (evalRules(rules)) {
                        auto addVal = [&](const QString &s) {
                            QString sv = substPlaceholders(s);
                            if (javaMajor > 0 && javaMajor < 22) {
                                if (sv.startsWith("--sun-misc-unsafe-memory-access=") ||
                                    sv.startsWith("--enable-native-access="))
                                    return;
                            }
                            if (sv == "-p") useModulePath = true;
                            verJvmArgs << sv;
                        };
                        if (val.isString())
                            addVal(val.toString());
                        else if (val.isArray())
                            for (const auto &v : val.toArray())
                                if (v.isString())
                                    addVal(v.toString());
                    }
                }
            }
        }
    }

    // Write legacyClassPath.file for Forge's BootstrapLauncher
    QString cpFilePath = m_mcDir + "/classpath.txt";
    {
        QFile cf(cpFilePath);
        if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            cf.write(QString(classpathStr).replace(';', '\n').toUtf8());
            cf.close();
        }
    }

    // Append module-aware args after existing -Xmx and extra JVM args
    jvmArgs << QString("-DlegacyClassPath.file=%1").arg(cpFilePath);

    // Module args from version JSON
    jvmArgs << verJvmArgs;

    // Natives -Djava.library.path
    jvmArgs << QString("-Djava.library.path=%1").arg(nativesDirStr);

    // Classpath (-cp) only when not using module path to avoid split-package conflicts
    if (!useModulePath)
        jvmArgs << "-cp" << classpathStr;

    // Main class
    jvmArgs << QString::fromUtf8(m_vs.ver.main_class);

    // Game arguments
    QStringList gameArgs;

    if (root.contains("arguments") && root["arguments"].isObject()) {
        auto argsObj = root["arguments"].toObject();
        if (argsObj.contains("game") && argsObj["game"].isArray()) {
            auto game = argsObj["game"].toArray();

            bool hasStandardArgs = false;
            for (const auto &arg : game) {
                if (arg.isString()) {
                    QString s = arg.toString();
                    if (s == "--username" || s == "--version" || s == "--accessToken") {
                        hasStandardArgs = true;
                        break;
                    }
                }
            }

            if (!hasStandardArgs) {
                gameArgs << "--username" << username;
                gameArgs << "--version" << m_versionId;
                gameArgs << "--gameDir" << m_mcDir;
                gameArgs << "--assetsDir" << QString::fromUtf8(assetsDir);
                gameArgs << "--assetIndex" << QString::fromUtf8(m_vs.ver.assets);
                gameArgs << "--uuid" << uuid;
                gameArgs << "--accessToken" << accessToken;
                gameArgs << "--userType" << (m_session.is_authenticated ? QString::fromUtf8(m_session.user_type) : "offline");
                gameArgs << "--versionType" << QString::fromUtf8(m_vs.ver.type);
            }

            for (const auto &arg : game) {
                if (arg.isString()) {
                    gameArgs << substPlaceholders(arg.toString());
                } else if (arg.isObject()) {
                    auto obj = arg.toObject();
                    QJsonArray rules = obj["rules"].toArray();
                    QJsonValue val = obj["value"];
                    if (evalRules(rules)) {
                        if (val.isString())
                            gameArgs << substPlaceholders(val.toString());
                        else if (val.isArray())
                            for (const auto &v : val.toArray())
                                if (v.isString())
                                    gameArgs << substPlaceholders(v.toString());
                    }
                }
            }
        }
    } else if (m_vs.ver.minecraft_arguments[0]) {
        // Properly split minecraftArguments using Windows CommandLineToArgvW
        // to handle paths with spaces after placeholder substitution
        QString substituted = substPlaceholders(QString::fromUtf8(m_vs.ver.minecraft_arguments));
        int argc = 0;
        wchar_t **argv = CommandLineToArgvW((LPCWSTR)substituted.utf16(), &argc);
        if (argv) {
            for (int i = 0; i < argc; ++i)
                gameArgs << QString::fromWCharArray(argv[i]);
            LocalFree(argv);
        }
    }

    QStringList allArgs = jvmArgs + gameArgs;

    // Launch
    m_running = true;
    emit runningChanged();
    emit launchStarted();

    m_process = new QProcess(this);
    m_process->setProgram(m_javaPath);
    m_process->setArguments(allArgs);
    m_process->setWorkingDirectory(m_mcDir);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    m_process->setProcessEnvironment(env);
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });

    // Open game output log file
    QString gameLogPath = m_mcDir + "/game_output.log";
    QFile *gameLog = new QFile(gameLogPath);
    if (gameLog->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        mc_info("[Launch] Game output log: %s", gameLogPath.toUtf8().constData());
    else
        { delete gameLog; gameLog = nullptr; }
    m_gameLog = gameLog;

    connect(m_process, &QProcess::started, this, [this]() {
        mc_info("Game process started");
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_process->canReadLine()) {
            QString line = QString::fromLocal8Bit(m_process->readLine()).trimmed();
            if (!line.isEmpty()) {
                if (m_gameLog) {
                    m_gameLog->write(line.toUtf8());
                    m_gameLog->write("\n");
                    m_gameLog->flush();
                }
                fprintf(stderr, "[GAME] %s\n", line.toUtf8().constData());
                emit logLine(line);
            }
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        mc_error("QProcess error: %d (%s)", (int)err,
                 m_process ? m_process->errorString().toUtf8().constData() : "null");
        if (m_gameLog) { m_gameLog->close(); delete m_gameLog; m_gameLog = nullptr; }
        m_running = false;
        emit runningChanged();
        emit errorOccurred("进程启动失败: " + (m_process ? m_process->errorString() : QString("unknown")));
        if (m_process) {
            m_process->deleteLater();
            m_process = nullptr;
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        mc_info("[Launch] Game process exited with code %d", exitCode);
        if (m_gameLog) { m_gameLog->close(); delete m_gameLog; m_gameLog = nullptr; }
        m_running = false;
        emit runningChanged();
        emit launchCompleted(exitCode);
        m_process->deleteLater();
        m_process = nullptr;
    });

    QString cmdLine = m_javaPath + " " + allArgs.join(' ');
    mc_info("[Launch] cmd (first 3000): %s", cmdLine.left(3000).toUtf8().constData());
    m_process->start();

    // Save full command line for debugging
    QFile cmdFile(m_mcDir + "/mclaunch_cmdline.txt");
    if (cmdFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        cmdFile.write(cmdLine.toUtf8());

    if (m_vs.verLoaded) mc_version_free(&m_vs.ver);
}

void LaunchManager::stop()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(5000);
    }
}
