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
#include "InstallManager.h"
#include <mc_log.h>
#include <mc_path.h>
#include <mc_http.h>
#include <mc_download_qt.h>
#include <zlib.h>
#include <mc_download.h>
#include <mc_library.h>
#include <mc_hash.h>
#include <mc_str.h>
#include <mc_version.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QThread>
#include <QProcess>
#include <QEventLoop>
#include <QXmlStreamReader>
#include <cstdlib>
#include <functional>
#include <algorithm>
#include "KernelBridge.h"

// PCLCE-style download: try BMCLAPI first, then official URL, with up to 4 retry attempts and random delay
static bool isMojangUrl(const QString &url) {
    return url.contains("piston-data.mojang.com") ||
           url.contains("piston-meta.mojang.com") ||
           url.contains("launcher.mojang.com") ||
           url.contains("launchermeta.mojang.com") ||
           url.contains("libraries.minecraft.net") ||
           url.contains("resources.download.minecraft.net");
}

static bool mc_download_pclce(const char *url, const char *path, const char *sha1, long size, int timeout) {
    char mirrorUrl[2048];
    const char *urls[2];
    QByteArray mirrorBuf;
    if (mc_download_translate_mojang_url_auto(url, mirrorUrl, sizeof(mirrorUrl)) && mirrorUrl[0]) {
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

#ifdef Q_OS_WIN
#include <windows.h>
#include <excpt.h>
#endif

static QJsonObject httpGetJson(const QString &url);
static QByteArray httpGetRaw(const QString &url);
static QByteArray httpGetRawDirect(const QString &url);

// Forward declarations for Forge helpers
static QStringList forgeVersionsFromMaven(const QString &mcVersion);
static QJsonObject forgeProfileFromInstaller(const QByteArray &jarData);
static QByteArray extractFromJar(const QByteArray &jarData, const QString &entryPath);

// Synchronous loader installer. Reused by both InstallWorker and ModpackManager.
// Returns true on success; on failure sets *errorOut and returns false.
// *outVerId receives the installed version id (may be null).
// progress receives (0..1, status) updates. Safe to call from any thread.
bool installLoaderSync(const QString &mcVersion, const QString &loader,
                       const QString &loaderVer, const QString &javaPath,
                       const QString &dir, QString *errorOut, QString *outVerId,
                       const std::function<void(qreal, const QString&)> &progress);

// Translate an official URL using the kernel's mirror config.
// Returns the mirrored URL if a matching rule is found, or the original URL.
static QString translateUrl(const QString &url)
{
    QString host;
    int schemeEnd = url.indexOf("://");
    if (schemeEnd >= 0) {
        int pathStart = url.indexOf('/', schemeEnd + 3);
        host = (pathStart >= 0) ? url.mid(schemeEnd + 3, pathStart - schemeEnd - 3)
                                : url.mid(schemeEnd + 3);
    }
    if (host.isEmpty()) return url;

    int count = mc_mirror_config_count();
    for (int i = 0; i < count; ++i) {
        const McMirrorEntry *entry = mc_mirror_config_get(i);
        if (!entry) continue;
        for (int r = 0; r < entry->rule_count; ++r) {
            const McMirrorRule *rule = &entry->rules[r];
            if (host.contains(rule->match)) {
                QString base = QString::fromUtf8(entry->base_url);
                if (!base.endsWith('/')) base += '/';
                QString prefix = QString::fromUtf8(rule->prefix);
                if (prefix.startsWith('/')) prefix = prefix.mid(1);
                if (!prefix.isEmpty() && !prefix.endsWith('/')) prefix += '/';
                int pathStart = url.indexOf('/', schemeEnd + 3);
                QString rest = (pathStart >= 0) ? url.mid(pathStart + 1) : QString();
                return base + prefix + rest;
            }
        }
    }
    return url;
}

// Extract library download URL from JSON object.
// Handles legacy format  ({"url": "...", "name": "..."})
// and modern format    ({"downloads": {"artifact": {"url": "...", "sha1": "..."}}, "name": "..."})
static QString extractLibUrl(const QJsonObject &lib)
{
    QString url = lib["url"].toString();
    if (!url.isEmpty()) return url;
    QJsonObject dl = lib["downloads"].toObject();
    if (!dl.isEmpty()) {
        url = dl["artifact"].toObject()["url"].toString();
        if (!url.isEmpty()) return url;
        url = dl["url"].toString();
        if (!url.isEmpty()) return url;
    }
    return QString();
}

// Resolve library download URL following kernel installer pattern:
//   - empty baseUrl → use mc_library_resolve_url with default mirror
//   - baseUrl ends with ".jar" → use as-is
//   - otherwise → baseUrl + "/" + maven_path
static QString resolveLibUrl(const QString &baseUrl, const QString &name)
{
    if (baseUrl.isEmpty()) {
        char buf[2048];
        mc_library_resolve_url(name.toUtf8().constData(), nullptr, buf, sizeof(buf));
        return QString::fromUtf8(buf);
    }
    if (baseUrl.endsWith(".jar", Qt::CaseInsensitive))
        return baseUrl;
    char libPath[1024];
    mc_library_resolve_path(name.toUtf8().constData(), libPath, sizeof(libPath));
    QString base = baseUrl;
    if (!base.endsWith('/'))
        base += '/';
    return base + QString::fromUtf8(libPath);
}

// ─── Worker (lives in worker thread) ────────────────────────────────────────
class InstallWorker : public QObject
{
    Q_OBJECT

public:
    explicit InstallWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void doInstall(const QString &mcVersion, const QString &loader,
                   const QString &loaderVer, const QString &javaPath,
                   const QString &dir)
    {
        QString error, verId;
        bool ok = installLoaderSync(
            mcVersion, loader, loaderVer, javaPath, dir, &error, &verId,
            [this](qreal p, const QString &s) { reportStep(p, s); });
        if (ok)
            emit installCompleted(verId);
        else
            emitError(error);
    }

signals:
    void progressReported(qreal progress, const QString &status);
    void installCompleted(const QString &verId);
    void errorOccurred(const QString &message);

private:
    void reportStep(qreal p, const QString &s) {
        emit progressReported(p, s);
    }

    void emitError(const QString &msg) {
        mc_error("[Install] ERROR: %s", msg.toUtf8().constData());
        emit errorOccurred(msg);
    }
};

static QString resolveLatestLoaderVersion(const QString &mcVersion, const QString &apiBase)
{
    QByteArray raw = httpGetRaw(QString("%1/%2").arg(apiBase, mcVersion));
    if (raw.isEmpty()) return QString();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return QString();
    QJsonArray items = doc.array();
    if (items.isEmpty()) return QString();
    QJsonObject first = items[0].toObject();
    QJsonObject loaderObj = first.value("loader").toObject();
    return loaderObj.value("version").toString();
}

// ─── Forge: fetch version list from official Maven metadata XML ───
// Compare Forge build numbers numerically (43.10.0 > 43.5.2), falling back to
// string comparison for non-numeric segments.
static bool forgeVersionLess(const QString &a, const QString &b)
{
    const QStringList as = a.split('.');
    const QStringList bs = b.split('.');
    int n = qMax(as.size(), bs.size());
    for (int i = 0; i < n; ++i) {
        bool aOk = false, bOk = false;
        long long av = (i < as.size()) ? as[i].toLongLong(&aOk) : 0;
        long long bv = (i < bs.size()) ? bs[i].toLongLong(&bOk) : 0;
        if (aOk && bOk) {
            if (av != bv) return av < bv;
        } else {
            QString asg = (i < as.size()) ? as[i] : QString();
            QString bsg = (i < bs.size()) ? bs[i] : QString();
            int cmp = asg.compare(bsg);
            if (cmp != 0) return cmp < 0;
        }
    }
    return false;
}

// PCL-CE style: BMCLAPI's dedicated Forge endpoint is fast and up to date
// (every MC version up to the latest). The mirror-translated maven-metadata
// snapshot is stale (missing everything above 1.18), so it must not be used.
static QStringList forgeVersionsFromBmclapi(const QString &mcVersion)
{
    QStringList versions;
    QByteArray raw = httpGetRaw(QString("https://bmclapi2.bangbang93.com/forge/minecraft/%1").arg(mcVersion));
    if (raw.isEmpty()) return versions;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return versions;

    QString prefix = mcVersion + "-";
    QJsonArray arr = doc.array();
    for (const auto &item : arr) {
        QJsonObject obj = item.toObject();
        QString ver = obj.value("version").toString();
        if (ver.isEmpty() || ver == mcVersion) continue;
        if (ver.startsWith(prefix)) ver = ver.mid(prefix.length());
        if (!ver.isEmpty() && !versions.contains(ver))
            versions << ver;
    }
    return versions;
}

static QStringList forgeVersionsFromMaven(const QString &mcVersion)
{
    // Primary: BMCLAPI Forge endpoint.
    QStringList versions = forgeVersionsFromBmclapi(mcVersion);
    if (!versions.isEmpty())
        return versions;

    // Fallback: official Forge Maven metadata, fetched directly (bypassing the
    // mirror translation that rewrites maven.minecraftforge.net to a stale copy).
    QStringList parsed;
    QString prefix = mcVersion + "-";
    // The metadata XML is large and the server is occasionally flaky; retry with
    // a small backoff instead of giving up on the first network reset.
    for (int attempt = 1; attempt <= 3; ++attempt) {
        QByteArray xmlData = httpGetRawDirect("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");
        if (xmlData.isEmpty()) {
            mc_warn("[Install] Forge maven-metadata fetch attempt %d/3 failed", attempt);
            QThread::msleep(500 * attempt);
            continue;
        }

        QXmlStreamReader xml(xmlData);
        while (!xml.atEnd() && !xml.hasError()) {
            if (xml.readNext() == QXmlStreamReader::StartElement &&
                xml.name().toString() == "version") {
                QString ver = xml.readElementText();
                if (ver.startsWith(prefix)) {
                    // Extract the forge build number part
                    QString forgePart = ver.mid(prefix.length());
                    if (!forgePart.isEmpty() && !parsed.contains(forgePart))
                        parsed << forgePart;
                }
            }
        }
        break;
    }
    return parsed;
}

// ─── Forge: extract a named file from installer JAR bytes ───
static QByteArray extractFromJar(const QByteArray &jarData, const QString &targetName)
{
    const char *data = jarData.constData();
    int size = jarData.size();
    if (size < 22) return QByteArray();

    int eocd = -1;
    for (int i = size - 22; i >= 0; --i) {
        if ((unsigned char)data[i]   == 0x50 &&
            (unsigned char)data[i+1] == 0x4b &&
            (unsigned char)data[i+2] == 0x05 &&
            (unsigned char)data[i+3] == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return QByteArray();

    // ZIP entry names never start with '/', but the json field in
    // install_profile.json sometimes has a leading '/'
    QString searchName = targetName;
    if (searchName.startsWith('/'))
        searchName = searchName.mid(1);

    quint32 cdOffset = *(const quint32*)(data + eocd + 16);
    int idx = (int)cdOffset;
    while (idx + 46 <= size) {
        if ((unsigned char)data[idx]   != 0x50 ||
            (unsigned char)data[idx+1] != 0x4b ||
            (unsigned char)data[idx+2] != 0x01 ||
            (unsigned char)data[idx+3] != 0x02)
            break;

        quint16 nameLen   = *(const quint16*)(data + idx + 28);
        quint16 extraLen  = *(const quint16*)(data + idx + 30);
        quint16 commentLen= *(const quint16*)(data + idx + 32);
        quint32 localOff  = *(const quint32*)(data + idx + 42);

        QString fname = QString::fromUtf8(data + idx + 46, nameLen);
        if (fname != searchName) {
            idx += 46 + nameLen + extraLen + commentLen;
            continue;
        }

        if (localOff + 30 > (quint32)size) break;
        quint32 compSz = *(const quint32*)(data + idx + 20);
        quint32 ucSz   = *(const quint32*)(data + idx + 24);
        quint16 method = *(const quint16*)(data + idx + 10);
        quint16 fnl    = *(const quint16*)(data + localOff + 26);
        quint16 el     = *(const quint16*)(data + localOff + 28);

        quint32 dataOff = localOff + 30 + fnl + el;
        if (dataOff + compSz > (quint32)size) break;

        QByteArray out;
        if (method == 0) {
            out = QByteArray(data + dataOff, compSz);
        } else if (method == 8) {
            if (ucSz > 0 && ucSz < 16 * 1024 * 1024) {
                out.resize((int)ucSz);
                z_stream strm;
                memset(&strm, 0, sizeof(strm));
                strm.next_in = (Bytef*)(data + dataOff);
                strm.avail_in = compSz;
                strm.next_out = (Bytef*)out.data();
                strm.avail_out = out.size();
                if (inflateInit2(&strm, -MAX_WBITS) != Z_OK ||
                    inflate(&strm, Z_FINISH) != Z_STREAM_END)
                    out.clear();
                else
                    out.resize((int)strm.total_out);
                inflateEnd(&strm);
            }
        }
        return out;
    }
    return QByteArray();
}

// ─── Forge: extract version profile from installer JAR ───
static QJsonObject forgeProfileFromInstaller(const QByteArray &jarData)
{
    QByteArray ipData = extractFromJar(jarData, "install_profile.json");
    if (ipData.isEmpty()) return QJsonObject();

    QJsonDocument doc = QJsonDocument::fromJson(ipData);
    if (doc.isNull() || !doc.isObject()) return QJsonObject();
    QJsonObject ip = doc.object();

    // Old format: versionInfo embedded directly
    if (ip.contains("versionInfo"))
        return ip["versionInfo"].toObject();

    // New format (spec 0/1): json field points to another file in the JAR
    if (ip.contains("json") && ip["json"].isString()) {
        QString jsonPath = ip["json"].toString();
        QByteArray verData = extractFromJar(jarData, jsonPath);
        if (!verData.isEmpty()) {
            QJsonDocument vdoc = QJsonDocument::fromJson(verData);
            if (!vdoc.isNull() && vdoc.isObject())
                return vdoc.object();
        }
    }
    return QJsonObject();
}

// Locate a usable JVM for the official Forge installer: bundled runtimes first,
// then JAVA_HOME. Returns an empty string if none is available.
static QString findJavaForInstall(const QString &mcDir)
{
    QStringList candidates;
    candidates << QDir(mcDir).filePath("../.runtime/java-17/bin/java.exe")
               << QDir(mcDir).filePath("../.runtime/java-11/bin/java.exe")
               << QDir(mcDir).filePath("../.runtime/java-21/bin/java.exe")
               << QDir(mcDir).filePath("../.runtime/java-8/bin/java.exe");
    const char *jh = getenv("JAVA_HOME");
    if (jh && jh[0])
        candidates << QString::fromUtf8(jh) + "/bin/java.exe";
    for (const QString &c : candidates) {
        QFileInfo fi(c);
        if (fi.exists() && fi.isFile() && fi.size() > 0)
            return QDir::cleanPath(c);
    }
    return QString();
}

// ─── Synchronous loader installer (shared by InstallWorker + ModpackManager) ───

bool installLoaderSync(const QString &mcVersion, const QString &loader,
                       const QString &loaderVer, const QString &javaPath,
                       const QString &dir, QString *errorOut, QString *outVerId,
                       const std::function<void(qreal, const QString&)> &progress)
{
    auto fail = [errorOut](const QString &msg) -> bool {
        mc_error("[Install] ERROR: %s", msg.toUtf8().constData());
        if (errorOut) *errorOut = msg;
        return false;
    };

    mc_info("[Install] === START loader=%s mc=%s ver=%s dir=%s",
            loader.toUtf8().constData(), mcVersion.toUtf8().constData(),
            loaderVer.toUtf8().constData(), dir.toUtf8().constData());

    QString mcDir = QDir::toNativeSeparators(dir);
    QByteArray forgeInstallerJar;
    QJsonObject forgeInstallProfile;

    // Step 1: Fetch loader profile JSON
    progress(0.05, "Fetching profile");

    QJsonObject profile;
    QString verId, inheritsFrom;
    bool noLoaderVer = (loaderVer.isEmpty() || loaderVer == "latest");

    if (loader == "fabric") {
        QString resolvedVer = loaderVer;
        if (resolvedVer.isEmpty() || resolvedVer == "latest") {
            resolvedVer = resolveLatestLoaderVersion(mcVersion, "https://meta.fabricmc.net/v2/versions/loader");
            if (resolvedVer.isEmpty()) return fail("Failed to find latest Fabric version");
        }
        QString url = QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json")
                          .arg(mcVersion, resolvedVer);
        mc_info("[Install] Fabric URL: %s", url.toUtf8().constData());
        profile = httpGetJson(url);
        if (profile.isEmpty()) return fail("Failed to fetch Fabric profile");
        verId = profile["id"].toString();
        inheritsFrom = profile["inheritsFrom"].toString();
        mc_info("[Install] Fabric profile: id=%s inherits=%s", verId.toUtf8().constData(), inheritsFrom.toUtf8().constData());

    } else if (loader == "quilt") {
        QString resolvedVer = loaderVer;
        if (resolvedVer.isEmpty() || resolvedVer == "latest") {
            resolvedVer = resolveLatestLoaderVersion(mcVersion, "https://meta.quiltmc.org/v3/versions/loader");
            if (resolvedVer.isEmpty()) return fail("Failed to find latest Quilt version");
        }
        QString url = QString("https://meta.quiltmc.org/v3/versions/loader/%1/%2/profile/json")
                          .arg(mcVersion, resolvedVer);
        mc_info("[Install] Quilt URL: %s", url.toUtf8().constData());
        profile = httpGetJson(url);
        if (profile.isEmpty()) return fail("Failed to fetch Quilt profile");
        verId = profile["id"].toString();
        inheritsFrom = profile["inheritsFrom"].toString();

    } else if (loader == "forge") {
        QString forgeVer = noLoaderVer ? QString() : loaderVer;
        if (forgeVer.isEmpty()) {
            // Fetch version list from official Forge Maven metadata
            QStringList vers = forgeVersionsFromMaven(mcVersion);
            if (vers.isEmpty()) return fail("Failed to find any Forge version");
            // Take the latest in numerical order (43.10.0 > 43.5.2)
            std::sort(vers.begin(), vers.end(), forgeVersionLess);
            forgeVer = vers.last();
            mc_info("[Install] Latest Forge for %s: %s", mcVersion.toUtf8().constData(), forgeVer.toUtf8().constData());
        }
        // Download installer JAR
        QString fullVerStr = mcVersion + "-" + forgeVer;
        QString jarUrl = QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(fullVerStr);
        mc_info("[Install] Downloading Forge installer: %s", jarUrl.toUtf8().constData());
        forgeInstallerJar = httpGetRaw(jarUrl);
        if (forgeInstallerJar.isEmpty()) return fail("Failed to download Forge installer");

        // Extract version profile from installer
        profile = forgeProfileFromInstaller(forgeInstallerJar);
        if (profile.isEmpty()) return fail("Failed to fetch Forge profile from installer");
        verId = profile["id"].toString();
        inheritsFrom = profile["inheritsFrom"].toString();
        mc_info("[Install] Forge profile: id=%s inherits=%s", verId.toUtf8().constData(), inheritsFrom.toUtf8().constData());

        // Save install_profile.json for Step 4 library extraction
        QByteArray ipRaw = extractFromJar(forgeInstallerJar, "install_profile.json");
        if (!ipRaw.isEmpty()) {
            QJsonDocument ipDoc = QJsonDocument::fromJson(ipRaw);
            if (ipDoc.isObject())
                forgeInstallProfile = ipDoc.object();
        }

    } else if (loader == "neoforge") {
        QString nfVer = noLoaderVer ? QString() : loaderVer;
        if (nfVer.isEmpty()) {
            QJsonObject listResp = httpGetJson(QString("https://api.neoforged.net/api/v2/mods/neoforge/versions?gameVersion=%1").arg(mcVersion));
            QJsonArray versions;
            if (listResp.contains("versions"))
                versions = listResp["versions"].toArray();
            else if (listResp.contains("data"))
                versions = listResp["data"].toArray();
            if (!versions.isEmpty()) {
                QJsonObject latest = versions.first().toObject();
                nfVer = latest["version"].toString();
                if (nfVer.isEmpty()) nfVer = latest["displayName"].toString();
            }
            if (nfVer.isEmpty()) return fail("Failed to find latest NeoForge version");
        }
        profile = httpGetJson(QString("https://api.neoforged.net/api/v2/mods/neoforge/versions/%1/downloads/client").arg(nfVer));
        if (profile.isEmpty()) return fail("Failed to fetch NeoForge profile");
        verId = profile["id"].toString();
        inheritsFrom = profile["inheritsFrom"].toString();
    } else {
        return fail(QString("Unknown loader: %1").arg(loader));
    }

    if (verId.isEmpty()) return fail("Invalid loader profile: missing version ID");

    // Step 2: Save profile JSON
    progress(0.15, "Saving profile");

    QString verDir = QDir(mcDir).filePath("versions/" + verId);
    QString verJson = verDir + "/" + verId + ".json";
    QDir().mkpath(verDir);

    {   QFile f(verJson);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(profile).toJson());
    }

    // Step 3: Collect libraries
    progress(0.25, "Collecting libraries");
    QJsonArray allLibs = profile["libraries"].toArray();
    mc_info("[Install] Profile has %d libraries", allLibs.size());

    if (!inheritsFrom.isEmpty()) {
        mc_info("[Install] Merging parent libraries from %s", inheritsFrom.toUtf8().constData());
        QString parentJson = QDir(mcDir).filePath("versions/" + inheritsFrom + "/" + inheritsFrom + ".json");

        if (!QFileInfo::exists(parentJson)) {
            mc_info("[Install] Fetching parent version JSON");
            QDir().mkpath(QFileInfo(parentJson).absolutePath());
            McVersion *pf = new McVersion; mc_version_init(pf);
            if (mc_version_fetch_by_id_mirror(pf, inheritsFrom.toUtf8().constData(), "mojang")) {
                QFile pfJson(parentJson);
                if (pfJson.open(QIODevice::WriteOnly))
                    pfJson.write(QJsonDocument(pf->raw_json).toJson());
            }
            mc_version_free(pf); delete pf;
        }

        if (QFileInfo::exists(parentJson)) {
            QFile pf(parentJson);
            if (pf.open(QIODevice::ReadOnly)) {
                QJsonArray parentLibs = QJsonDocument::fromJson(pf.readAll()).object()["libraries"].toArray();
                mc_info("[Install] Parent has %d libraries", parentLibs.size());
                for (const auto &pl : parentLibs) {
                    QString n = pl.toObject()["name"].toString();
                    if (!n.isEmpty()) {
                        bool dup = false;
                        for (const auto &al : allLibs)
                            if (al.toObject()["name"].toString() == n) { dup = true; break; }
                        if (!dup) allLibs.append(pl);
                    }
                }
                mc_info("[Install] Total libraries after merge: %d", allLibs.size());
            }
        }
    }

    // Step 4: Download libraries
    progress(0.30, "Preparing library downloads");

    QStringList libUrls, libPaths, libSha1s, libOriginalUrls;
    QList<long> libSizes;
    QString libsDir = QDir(mcDir).filePath("libraries");

    for (int i = 0; i < allLibs.size(); ++i) {
        QJsonObject lib = allLibs[i].toObject();
        QString name = lib["name"].toString();
        if (name.isEmpty()) continue;

        // Extract URL, SHA1, and size: modern format (downloads.artifact) or legacy
        QString fullUrl = extractLibUrl(lib);
        QString sha1 = lib["sha1"].toString();
        if (sha1.isEmpty())
            sha1 = lib["downloads"].toObject()["artifact"].toObject()["sha1"].toString();
        long libSize = lib["downloads"].toObject()["artifact"].toObject()["size"].toDouble(0);
        if (libSize <= 0) libSize = lib["size"].toDouble(0);
        QString natives = lib["natives"].toObject()[QString::fromUtf8(mc_platform_get())].toString();

        char libPath[1024];
        mc_library_resolve_path(name.toUtf8().constData(), libPath, sizeof(libPath));
        QString fullPath = QDir(libsDir).filePath(QString::fromUtf8(libPath));

        // Skip only if file exists, has reasonable size, SHA1 matches (or is valid JAR)
        if (QFileInfo::exists(fullPath) && QFileInfo(fullPath).size() > 0) {
            qint64 fsz = QFileInfo(fullPath).size();
            if (sha1.isEmpty()) {
                // No SHA1: verify it's a valid JAR (ZIP magic) and not an HTML error page
                QFile ft(fullPath);
                if (ft.open(QIODevice::ReadOnly)) {
                    QByteArray head = ft.read(200);
                    ft.close();
                    bool valid = head.size() >= 2 && head[0] == 'P' && head[1] == 'K'
                                 && fsz >= 256
                                 && !head.contains("<html") && !head.contains("<!DOC");
                    if (valid) continue;
                    mc_info("[Install] %s is corrupt/empty (%lld bytes), redownloading",
                            name.toUtf8().constData(), fsz);
                    QFile::remove(fullPath);
                }
            } else {
                char existing[64];
                if (mc_hash_file_sha1(fullPath.toUtf8().constData(), existing, sizeof(existing)) &&
                    QString::fromUtf8(existing) == sha1)
                    continue; // SHA1 matches
                mc_info("[Install] SHA1 mismatch for %s, redownloading", name.toUtf8().constData());
            }
        }

        QString origUrl = resolveLibUrl(fullUrl, name);
        if (origUrl.isEmpty()) continue;

        QString dlUrl = origUrl;
        if (isMojangUrl(origUrl)) {
            char translated[2048];
            if (mc_download_translate_mojang_url_auto(origUrl.toUtf8().constData(), translated, sizeof(translated)))
                dlUrl = QString::fromUtf8(translated);
        }

        libUrls << dlUrl;
        libOriginalUrls << origUrl;
        libPaths << fullPath;
        libSha1s << sha1;
        libSizes << libSize;

        // Handle natives
        if (!natives.isEmpty()) {
            QString nUrl = lib["downloads"].toObject()["classifiers"].toObject()[natives].toObject()["url"].toString();
            if (nUrl.isEmpty()) {
                QString classifierUrl = lib["classifier_url"].toString();
                if (!classifierUrl.isEmpty() && classifierUrl.endsWith(".jar", Qt::CaseInsensitive)) {
                    nUrl = classifierUrl;
                } else {
                    QString baseUrl = lib["url"].toString();
                    if (!baseUrl.isEmpty()) {
                        char nativePath[1024];
                        mc_library_natives_path(name.toUtf8().constData(), natives.toUtf8().constData(), nativePath, sizeof(nativePath));
                        QString base = baseUrl;
                        if (!base.endsWith('/')) base += '/';
                        nUrl = base + QString::fromUtf8(nativePath);
                    }
                }
            }
            if (!nUrl.isEmpty()) {
                QJsonObject classifier = lib["downloads"].toObject()["classifiers"].toObject()[natives].toObject();
                QString nSha1 = classifier["sha1"].toString();
                if (nSha1.isEmpty()) nSha1 = lib["classifier_sha1"].toString();
                char nativePath[1024];
                mc_library_natives_path(name.toUtf8().constData(), natives.toUtf8().constData(), nativePath, sizeof(nativePath));
                QString fnp = QDir(libsDir).filePath(QString::fromUtf8(nativePath));
                bool needNative = !QFileInfo::exists(fnp) || QFileInfo(fnp).size() == 0;
                if (!needNative && !nSha1.isEmpty()) {
                    char existing[64];
                    if (mc_hash_file_sha1(fnp.toUtf8().constData(), existing, sizeof(existing)) &&
                        QString::fromUtf8(existing) == nSha1)
                        needNative = false;
                    else
                        needNative = true;
                }
                if (needNative) {
                    QString origNUrl = nUrl;
                    if (isMojangUrl(nUrl)) {
                        char nt[2048];
                        if (mc_download_translate_mojang_url_auto(nUrl.toUtf8().constData(), nt, sizeof(nt)))
                            nUrl = QString::fromUtf8(nt);
                    }
                    long nSize = classifier["size"].toDouble(0);
                    if (nSize <= 0) nSize = lib["classifier_size"].toDouble(0);
                    libUrls << nUrl;
                    libOriginalUrls << origNUrl;
                    libPaths << fnp;
                    libSha1s << nSha1;
                    libSizes << nSize;
                }
            }
        }
    }

    mc_info("[Install] %d library files to download", libUrls.size());
    progress(0.35, "Downloading libraries");

    if (!libUrls.isEmpty()) {
        for (const auto &p : libPaths)
            QDir().mkpath(QFileInfo(p).absolutePath());

        int count = libUrls.size();
        std::vector<const char *> cUrls(count), cPaths(count), cSha1s(count), cOrig(count);
        std::vector<QByteArray> urlBa(count), pathBa(count), sha1Ba(count), origBa(count);
        std::vector<int> results(count, 0);
        std::vector<long> sizes(count);

        for (int i = 0; i < count; ++i) {
            urlBa[i] = libUrls[i].toUtf8();
            pathBa[i] = libPaths[i].toUtf8();
            origBa[i] = libOriginalUrls[i].toUtf8();
            cUrls[i] = urlBa[i].constData();
            cPaths[i] = pathBa[i].constData();
            cOrig[i] = origBa[i].constData();
            sizes[i] = (i < libSizes.size()) ? libSizes[i] : 0;
            if (libSha1s[i].isEmpty()) {
                sha1Ba[i] = QByteArray();
                cSha1s[i] = nullptr;
            } else {
                sha1Ba[i] = libSha1s[i].toUtf8();
                cSha1s[i] = sha1Ba[i].constData();
            }
        }

        mc_info("[Install] Calling mc_qt_download_batch (count=%d)...", count);
        mc_qt_download_batch(cUrls.data(), cPaths.data(), cSha1s.data(),
                             sizes.data(), count, 20000, results.data());

        int ok = 0;
        for (int i = 0; i < count; ++i) ok += results[i];
        mc_info("[Install] Downloaded %d/%d library files", ok, count);

        // Post-verify: detect HTML error pages or non-ZIP files with no SHA1/size
        auto postVerify = [&](const char *phase) {
            int n = 0;
            for (int i = 0; i < count; ++i) {
                if (!results[i]) continue;
                QFileInfo fi(libPaths[i]);
                if (!fi.exists() || fi.size() == 0) {
                    mc_info("[Install] %s: %s is missing or empty", phase, qUtf8Printable(libPaths[i]));
                    QFile::remove(libPaths[i]); results[i] = 0; n++;
                    continue;
                }
                QFile f(libPaths[i]);
                if (!f.open(QIODevice::ReadOnly)) continue;
                QByteArray head = f.read(200);
                f.close();
                bool corrupt = false;
                if (head.contains("<html") || head.contains("<!DOC") || head.contains("<HTML"))
                    corrupt = true;
                else if (fi.size() < 1000 && head.size() >= 2 && (head[0] != 'P' || head[1] != 'K'))
                    corrupt = true;
                if (corrupt) {
                    mc_info("[Install] %s: %s is corrupt (size=%lld), removing",
                            phase, qUtf8Printable(libPaths[i]), fi.size());
                    QFile::remove(libPaths[i]); results[i] = 0; n++;
                }
            }
            return n;
        };
        postVerify("Post-verify");

        // Retry failed files individually with fallback URLs (PCLCE pattern)
        ok = 0;
        for (int i = 0; i < count; ++i) ok += results[i];
        if (ok < count) {
            mc_info("[Install] Retrying %d failed downloads individually...", count - ok);
            for (int i = 0; i < count; ++i) {
                if (!results[i]) {
                    bool retryOk = false;
                    const char *retryUrls[2] = {cOrig[i], cUrls[i]};
                    for (int r = 0; r < 4 && !retryOk; ++r) {
                        QThread::msleep(300 + (std::rand() % (200 + r * 300)));
                        retryOk = mc_qt_download_file_multi(
                            retryUrls, 2, cPaths[i], cSha1s[i], 0, 120000);
                    }
                    if (!retryOk) {
                        mc_info("[Install] Retry FAIL: %s", cPaths[i]);
                    } else {
                        mc_info("[Install] Retry OK: %s", cPaths[i]);
                    }
                }
            }
            // Post-verify retried files too (retry has no SHA1/size check for bare libraries)
            int bad = postVerify("Retry-verify");
            if (bad)
                mc_info("[Install] Retry-verify caught %d still-corrupt files", bad);
        }
    }

    // Step 4c: Extract forge JAR from installer if needed
    mc_info("[Install] Step 4c: installerJarSize=%d installProfile.isEmpty=%d",
            forgeInstallerJar.size(), forgeInstallProfile.isEmpty());
    if (!forgeInstallerJar.isEmpty() && !forgeInstallProfile.isEmpty()) {
        mc_info("[Install] Step 4c: forgeInstallProfile keys=%s",
                QStringList(forgeInstallProfile.keys()).join(",").toUtf8().constData());

        // Try Legacy 2 format: has "install" node with path + filePath
        QJsonObject install = forgeInstallProfile["install"].toObject();
        if (!install.isEmpty()) {
            mc_info("[Install] Step 4c: Legacy 2 format (install node)");
            QString filePath = install["filePath"].toString();
            QString path = install["path"].toString();
            if (!filePath.isEmpty() && !path.isEmpty()) {
                QByteArray jarContent = extractFromJar(forgeInstallerJar, filePath);
                if (!jarContent.isEmpty()) {
                    char buf[1024];
                    mc_library_resolve_path(path.toUtf8().constData(), buf, sizeof(buf));
                    QString destPath = QDir(libsDir).filePath(QString::fromUtf8(buf));
                    QDir().mkpath(QFileInfo(destPath).absolutePath());
                    QFile f(destPath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(jarContent);
                        mc_info("[Install] Extracted forge JAR (Legacy 2): %s (%d bytes)",
                                destPath.toUtf8().constData(), jarContent.size());
                    }
                }
            }
        }

        // Try spec 1 format: iterate libraries array for entries with empty URL
        QJsonArray libs = forgeInstallProfile["libraries"].toArray();
        if (!libs.isEmpty()) {
            mc_info("[Install] Step 4c: spec 1 format, %d libraries in profile", libs.size());
            for (int li = 0; li < libs.size(); ++li) {
                QJsonObject libObj = libs[li].toObject();
                QString name = libObj["name"].toString();
                QJsonObject artifact = libObj["downloads"].toObject()["artifact"].toObject();
                QString url = artifact["url"].toString();
                QString path = artifact["path"].toString();
                if (!url.isEmpty() || path.isEmpty()) continue;
                mc_info("[Install] Step 4c: extracting library %s from path=%s",
                        name.toUtf8().constData(), path.toUtf8().constData());
                QByteArray jarContent = extractFromJar(forgeInstallerJar, path);
                if (jarContent.isEmpty()) {
                    mc_info("[Install] Step 4c: extract failed, trying maven/ prefix");
                    jarContent = extractFromJar(forgeInstallerJar, "maven/" + path);
                }
                if (!jarContent.isEmpty()) {
                    QString destPath = QDir(libsDir).filePath(path);
                    QDir().mkpath(QFileInfo(destPath).absolutePath());
                    QFile f(destPath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(jarContent);
                        mc_info("[Install] Extracted: %s (%d bytes)",
                                destPath.toUtf8().constData(), jarContent.size());
                    }
                } else {
                    mc_info("[Install] Step 4c: could not extract %s from installer", path.toUtf8().constData());
                }
            }
        }
    }

    // Step 4b: Post-download check (kernel "Missing library" pattern)
    {
        progress(0.55, "Verifying libraries");
        QString pj = verDir + "/" + verId + ".json";
        if (QFileInfo::exists(pj)) {
            McVersion *v = new McVersion; mc_version_init(v);
            if (mc_version_parse_file(v, pj.toUtf8().constData())) {
                for (int i = 0; i < v->library_count; i++) {
                    McLibrary *lib = &v->libraries[i];
                    if (!lib->is_required) continue;
                    if (lib->is_natives && lib->classifier_url[0]) continue;
                    char rel[MC_PATH_MAX];
                    mc_library_resolve_path(lib->name, rel, sizeof(rel));
                    if (!rel[0]) continue;
                    QString fp = QDir(libsDir).filePath(QString::fromUtf8(rel));
                    if (QFileInfo::exists(fp) && QFileInfo(fp).size() > 0) {
                        if (!lib->sha1[0]) continue; // no SHA1, trust file
                        char existing[64];
                        if (mc_hash_file_sha1(fp.toUtf8().constData(), existing, sizeof(existing)) &&
                            QString::fromUtf8(existing) == QString::fromUtf8(lib->sha1))
                            continue; // SHA1 matches
                        mc_info("[Install] Post-check SHA1 mismatch for %s, redownloading", lib->name);
                    }
                    char libUrl[2048];
                    size_t ulen = strlen(lib->url);
                    if (ulen > 4 && memcmp(lib->url + ulen - 4, ".jar", 4) == 0)
                        qstrncpy(libUrl, lib->url, sizeof(libUrl));
                    else
                        mc_library_resolve_url(lib->name, lib->url[0] ? lib->url : nullptr, libUrl, sizeof(libUrl));
                    if (!libUrl[0]) continue;
                    mc_info("[Install] Post-check: downloading missing %s", lib->name);
                    mc_download_pclce(libUrl, fp.toUtf8().constData(),
                                      lib->sha1[0] ? lib->sha1 : nullptr, 0, 20000);
                }
            }
            mc_version_free(v); delete v;
        }
    }

    // Step 4.5: Forge — run the official installer (--installClient) so its
    // "processors" generate the recompiled/remapped client jars (client-*-srg.jar,
    // client-*-extra.jar, forge-*-client.jar) and write the canonical version
    // JSON. The lite install above only downloads the libraries.
    if (loader == "forge" && !forgeInstallerJar.isEmpty()) {
        progress(0.62, "Running official Forge installer");
        mc_info("[Install] Step 4.5: running official Forge installer for %s", verId.toUtf8().constData());

        QString javaExe = javaPath;
        if (javaExe.isEmpty() || !QFileInfo::exists(javaExe))
            javaExe = findJavaForInstall(mcDir);

        if (javaExe.isEmpty()) {
            mc_warn("[Install] No Java found for official Forge installer; skipped "
                    "(Forge client jars may be missing until a game is launched)");
        } else {
            // Pre-seed the vanilla client jar via the mirror so the installer
            // does not depend on Mojang's CDN for the recompilation step.
            QString ts;
            for (const auto &l : profile["libraries"].toArray()) {
                QString n = l.toObject()["name"].toString();
                if (n.startsWith("net.minecraft:client:")) {
                    ts = n.mid(QString("net.minecraft:client:").length());
                    break;
                }
            }
            if (!ts.isEmpty()) {
                char rel[MC_PATH_MAX];
                QString libName = "net.minecraft:client:" + ts;
                mc_library_resolve_path(libName.toUtf8().constData(), rel, sizeof(rel));
                QString seedPath = QDir(libsDir).filePath(QString::fromUtf8(rel));
                if (!(QFileInfo::exists(seedPath) && QFileInfo(seedPath).size() > 0)) {
                    QDir().mkpath(QFileInfo(seedPath).absolutePath());
                    QString seedUrl = QString("https://bmclapi2.bangbang93.com/version/%1/client").arg(inheritsFrom);
                    mc_info("[Install] Pre-seeding client jar for installer: %s", seedUrl.toUtf8().constData());
                    mc_download_pclce(seedUrl.toUtf8().constData(), seedPath.toUtf8().constData(), nullptr, 0, 30000);
                    if (!(QFileInfo::exists(seedPath) && QFileInfo(seedPath).size() > 0)) {
                        mc_warn("[Install] Client jar pre-seed failed; installer will try Mojang");
                        QFile::remove(seedPath);
                    }
                }
            }

            QString installerPath = QDir::temp().filePath(QString("forge-%1-installer.jar").arg(verId));
            {
                QFile f(installerPath);
                if (f.open(QIODevice::WriteOnly))
                    f.write(forgeInstallerJar);
            }

            // The official installer refuses a directory without a launcher
            // profile marker ("There is no minecraft launcher profile").
            {
                QString lpFile = QDir(mcDir).filePath("launcher_profiles.json");
                if (!QFileInfo::exists(lpFile)) {
                    QJsonObject prof;
                    prof["name"] = "test";
                    prof["lastVersionId"] = inheritsFrom;
                    QJsonObject profiles;
                    profiles["test"] = prof;
                    QJsonObject root;
                    root["profiles"] = profiles;
                    root["selectedProfile"] = "test";
                    root["clientToken"] = "00000000-0000-0000-0000-000000000000";
                    QJsonObject lv;
                    lv["name"] = "1.0";
                    lv["format"] = 21;
                    root["launcherVersion"] = lv;
                    QFile f(lpFile);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(QJsonDocument(root).toJson());
                        mc_info("[Install] Created launcher_profiles.json (needed by Forge installer)");
                    }
                }
            }

            if (QFileInfo::exists(installerPath) && QFileInfo(installerPath).size() > 0) {
                QProcess proc;
                proc.setProgram(javaExe);
                proc.setArguments(QStringList() << "-jar" << installerPath
                                   << "--installClient" << mcDir);
                proc.setProcessChannelMode(QProcess::MergedChannels);
                proc.start();
                if (proc.waitForStarted(15000)) {
                    if (!proc.waitForFinished(600000))
                        proc.kill();
                    QByteArray out = proc.readAll();
                    for (const QString &line : QString::fromUtf8(out).split('\n'))
                        if (!line.trimmed().isEmpty())
                            mc_info("[ForgeInstaller] %s", line.toUtf8().constData());
                    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
                        mc_info("[Install] Official Forge installer completed OK");
                    } else {
                        mc_error("[Install] Official Forge installer exited with code %d",
                                 proc.exitCode());
                    }
                } else {
                    mc_error("[Install] Failed to start Forge installer: %s",
                             proc.errorString().toUtf8().constData());
                }
                QFile::remove(installerPath);
            } else {
                mc_error("[Install] Could not write Forge installer to %s", installerPath.toUtf8().constData());
            }
        }
    }

    // Step 5: Ensure base version files
    if (!inheritsFrom.isEmpty()) {
        progress(0.70, "Ensuring base version");
        mc_info("[Install] Ensuring base version %s", inheritsFrom.toUtf8().constData());

        QString bvDir = QDir(mcDir).filePath("versions/" + inheritsFrom);
        QString bvJson = bvDir + "/" + inheritsFrom + ".json";

        if (!QFileInfo::exists(bvJson)) {
            mc_info("[Install] Fetching base version JSON");
            QDir().mkpath(bvDir);
            McVersion *bv = new McVersion; mc_version_init(bv);
            mc_version_fetch_by_id_mirror(bv, inheritsFrom.toUtf8().constData(), "mojang");
            if (!bv->raw_json.isEmpty()) {
                QFile bf(bvJson);
                if (bf.open(QIODevice::WriteOnly))
                    bf.write(QJsonDocument(bv->raw_json).toJson());
            }
            mc_version_free(bv); delete bv;
        }

        QString bvJar = bvDir + "/" + inheritsFrom + ".jar";
        bool needJar = !QFileInfo::exists(bvJar) || QFileInfo(bvJar).size() == 0;
        if (!needJar) {
            // Check SHA1
            McVersion *bv = new McVersion; mc_version_init(bv);
            if (mc_version_parse_file(bv, bvJson.toUtf8().constData()) && bv->client_sha1[0]) {
                char existing[64];
                if (mc_hash_file_sha1(bvJar.toUtf8().constData(), existing, sizeof(existing)) &&
                    QString::fromUtf8(existing) == QString::fromUtf8(bv->client_sha1))
                    needJar = false;
                else {
                    needJar = true;
                    mc_info("[Install] Base jar SHA1 mismatch, redownloading");
                }
            }
            mc_version_free(bv); delete bv;
        }
        if (needJar) {
            progress(0.75, "Downloading base client jar");
            mc_info("[Install] Downloading base client jar");
            McVersion *bv = new McVersion; mc_version_init(bv);
            if (mc_version_parse_file(bv, bvJson.toUtf8().constData()) && bv->client_url[0]) {
                mc_download_pclce(bv->client_url, bvJar.toUtf8().constData(),
                                  bv->client_sha1, bv->client_size, 30000);
                // A 0-byte (or SHA1-mismatched) jar must never pass as "installed":
                // the Forge processors and ModLauncher both rely on a real client jar.
                bool jarGood = QFileInfo::exists(bvJar) && QFileInfo(bvJar).size() > 0;
                if (jarGood && bv->client_sha1[0]) {
                    char existing[64];
                    jarGood = mc_hash_file_sha1(bvJar.toUtf8().constData(), existing, sizeof(existing)) &&
                              QString::fromUtf8(existing) == QString::fromUtf8(bv->client_sha1);
                }
                if (!jarGood) {
                    QFile::remove(bvJar);
                    mc_error("[Install] Base client jar invalid (0 bytes / SHA1 mismatch), removed: %s",
                             bvJar.toUtf8().constData());
                    mc_version_free(bv); delete bv;
                    return fail("原版客户端 jar 下载失败: " + inheritsFrom);
                }
            }
            mc_version_free(bv); delete bv;
        }

        progress(0.80, "Checking assets");
        McVersion *bv = new McVersion; mc_version_init(bv);
        if (mc_version_parse_file(bv, bvJson.toUtf8().constData())) {
            if (bv->asset_index.id[0]) {
                QString aif = QString("%1.json").arg(bv->asset_index.id);
                QString aip = QDir(QDir(mcDir).filePath("assets/indexes")).filePath(aif);
                if (!QFileInfo::exists(aip)) {
                    progress(0.82, "Downloading asset index");
                    QDir().mkpath(QFileInfo(aip).absolutePath());
                    mc_download_pclce(bv->asset_index.url, aip.toUtf8().constData(),
                                      bv->asset_index.sha1, 0, 30000);
                }
            }
            if (bv->logging_client_url[0]) {
                progress(0.85, "Downloading logging config");
                QString lurl = QString::fromUtf8(bv->logging_client_url);
                QString fn = lurl.mid(lurl.lastIndexOf('/') + 1);
                if (fn.isEmpty()) fn = "log4j2.xml";
                QString lp = QDir(QDir(mcDir).filePath("assets/logs")).filePath(fn);
                if (!QFileInfo::exists(lp)) {
                    QDir().mkpath(QFileInfo(lp).absolutePath());
                    mc_download_pclce(bv->logging_client_url, lp.toUtf8().constData(),
                                      bv->logging_client_sha1, 0, 30000);
                }
            }
        }
        mc_version_free(bv); delete bv;
    }

    mc_info("[Install] === COMPLETE ver=%s", verId.toUtf8().constData());
    if (outVerId) *outVerId = verId;
    progress(1.0, "Complete");
    return true;
}

// ─── Free helper: synchronous HTTP JSON/raw fetcher (thread-safe via thread_local QNAM) ───

static QJsonObject httpGetJson(const QString &url)
{
    QString translated = translateUrl(url);
    McHttpClient client;
    mc_http_init(&client);
    client.timeout_ms = 120000;
    McHttpResponse *resp = mc_http_get(&client, translated.toUtf8().constData());
    if (!resp) return QJsonObject();
    bool ok = resp->data && resp->data_len > 0 && resp->status_code >= 200 && resp->status_code < 300;
    if (!ok && url != translated) {
        mc_http_response_free(resp);
        mc_http_init(&client);
        resp = mc_http_get(&client, url.toUtf8().constData());
        if (!resp) return QJsonObject();
        ok = resp->data && resp->data_len > 0 && resp->status_code >= 200 && resp->status_code < 300;
    }
    QJsonObject obj;
    if (ok) {
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len));
        if (doc.isObject()) obj = doc.object();
    }
    mc_http_response_free(resp);
    return obj;
}

static QByteArray httpGetRaw(const QString &url)
{
    QString translated = translateUrl(url);
    McHttpClient client;
    mc_http_init(&client);
    client.timeout_ms = 120000;
    McHttpResponse *resp = mc_http_get(&client, translated.toUtf8().constData());
    if (!resp) return QByteArray();
    // Accept only 2xx status for successful data transfer
    bool ok = resp->data && resp->data_len > 0 && resp->status_code >= 200 && resp->status_code < 300;
    if (!ok && url != translated) {
        mc_http_response_free(resp);
        mc_http_init(&client);
        resp = mc_http_get(&client, url.toUtf8().constData());
        if (!resp) return QByteArray();
        ok = resp->data && resp->data_len > 0 && resp->status_code >= 200 && resp->status_code < 300;
    }
    if (!ok) {
        mc_http_response_free(resp);
        return QByteArray();
    }
    QByteArray data(resp->data, (int)resp->data_len);
    mc_http_response_free(resp);
    return data;
}

static QByteArray httpGetRawDirect(const QString &url)
{
    McHttpClient client;
    mc_http_init(&client);
    client.timeout_ms = 120000;
    McHttpResponse *resp = mc_http_get(&client, url.toUtf8().constData());
    if (!resp) return QByteArray();
    bool ok = resp->data && resp->data_len > 0 && resp->status_code >= 200 && resp->status_code < 300;
    if (!ok) {
        mc_http_response_free(resp);
        return QByteArray();
    }
    QByteArray data(resp->data, (int)resp->data_len);
    mc_http_response_free(resp);
    return data;
}

// ─── InstallManager (lives on main thread) ──────────────────────────────────

InstallManager::InstallManager(QObject *parent) : QObject(parent) {}

InstallManager::~InstallManager()
{
    if (m_workerThread) {
        m_workerThread->requestInterruption();
        m_workerThread->quit();
        if (!m_workerThread->wait(3000))
            m_workerThread->terminate();
    }
}

void InstallManager::cancelAll()
{
    if (m_workerThread) {
        m_workerThread->requestInterruption();
        m_workerThread->quit();
        if (!m_workerThread->wait(3000))
            m_workerThread->terminate();
        m_workerThread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
}

void InstallManager::setBusy(bool b)
{
    m_busy = b;
    emit busyChanged();
}

void InstallManager::installLoader(const QString &mcVersion, const QString &loader,
                                   const QString &loaderVer, const QString &javaPath,
                                   const QString &dir)
{
    if (m_busy) {
        mc_info("[Install] Already busy, ignoring");
        return;
    }

    // Clean up previous worker/thread (deleteLater handled by finished connections)
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    m_busy = true;
    m_progress = 0.0;
    m_status = QString("Installing %1...").arg(loader);
    emit busyChanged();
    emit progressChanged(0.0);
    emit statusChanged(m_status);

    mc_info("[Install] Creating worker thread for %s %s", loader.toUtf8().constData(), mcVersion.toUtf8().constData());

    // The official Forge installer needs a real JVM; make sure one is available
    // (bundled auto-download, same as the launch path) before dispatching.
    QString effectiveJava = javaPath;
    if (loader == "forge" && effectiveJava.isEmpty()) {
        if (JavaManager *jm = KernelBridge::instance()->javaManager()) {
            QVariantMap best = jm->findBest();
            if (!best.isEmpty()) {
                effectiveJava = best["path"].toString();
            } else {
                mc_info("[Install] No Java found; downloading Java 17 for the Forge installer");
                m_status = QString("正在准备 Java 17 (Forge 安装器需要)…");
                emit statusChanged(m_status);
                QEventLoop loop;
                auto c1 = QObject::connect(jm, &JavaManager::javaDownloaded, &loop,
                                           [&](const QString &path, int) { effectiveJava = path; loop.quit(); });
                auto c2 = QObject::connect(jm, &JavaManager::javaDownloadFailed, &loop,
                                           [&](int) { loop.quit(); });
                jm->downloadJava(17);
                loop.exec();
                QObject::disconnect(c1);
                QObject::disconnect(c2);
            }
        }
    }

    m_worker = new InstallWorker;
    m_workerThread = new QThread(this);
    m_workerThread->setStackSize(16 * 1024 * 1024);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, [w = m_worker, mcVersion, loader, loaderVer, effectiveJava, dir]() {
        w->doInstall(mcVersion, loader, loaderVer, effectiveJava, dir);
    }, Qt::DirectConnection);

    connect(m_worker, &InstallWorker::progressReported, this, [this](qreal p, const QString &s) {
        m_progress = p;
        m_status = s;
        emit progressChanged(p);
        emit statusChanged(s);
    }, Qt::QueuedConnection);

    connect(m_worker, &InstallWorker::errorOccurred, this, [this](const QString &msg) {
        mc_error("[Install] Worker error: %s", msg.toUtf8().constData());
        m_busy = false;
        emit busyChanged();
        emit errorOccurred(msg);
        m_workerThread->quit();
    }, Qt::QueuedConnection);

    connect(m_worker, &InstallWorker::installCompleted, this, [this](const QString &verId) {
        mc_info("[Install] Worker completed successfully: %s", verId.toUtf8().constData());
        m_busy = false;
        m_progress = 1.0;
        m_status = "Installation complete";
        emit busyChanged();
        emit progressChanged(1.0);
        emit statusChanged("Installation complete");
        emit installCompleted(verId);
        m_workerThread->quit();
    }, Qt::QueuedConnection);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_worker = nullptr;
        m_workerThread = nullptr;
    }, Qt::QueuedConnection);

    m_workerThread->start();
    mc_info("[Install] Worker thread started");
}

// Synchronous version list fetch (runs on worker thread)
static QStringList fetchLoaderVersionsSync(const QString &mcVersion, const QString &loader)
{
    QStringList versions;

    if (loader == "fabric") {
        QByteArray raw = httpGetRaw(QString("https://meta.fabricmc.net/v2/versions/loader/%1").arg(mcVersion));
        if (!raw.isEmpty()) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray arr = doc.array();
                for (const auto &item : arr) {
                    QJsonObject loaderObj = item.toObject().value("loader").toObject();
                    QString ver = loaderObj.value("version").toString();
                    if (!ver.isEmpty() && !versions.contains(ver))
                        versions << ver;
                }
            }
        }
    } else if (loader == "quilt") {
        QByteArray raw = httpGetRaw(QString("https://meta.quiltmc.org/v3/versions/loader/%1").arg(mcVersion));
        if (!raw.isEmpty()) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray arr = doc.array();
                for (const auto &item : arr) {
                    QJsonObject loaderObj = item.toObject().value("loader").toObject();
                    QString ver = loaderObj.value("version").toString();
                    if (!ver.isEmpty() && !versions.contains(ver))
                        versions << ver;
                }
            }
        }
    } else if (loader == "forge") {
        // Parse Maven metadata XML
        QStringList vers = forgeVersionsFromMaven(mcVersion);
        return vers;
    } else if (loader == "neoforge") {
        QJsonObject resp = httpGetJson(QString("https://api.neoforged.net/api/v2/mods/neoforge/versions?gameVersion=%1").arg(mcVersion));
        QJsonArray arr;
        if (resp.contains("versions"))
            arr = resp["versions"].toArray();
        else if (resp.contains("data"))
            arr = resp["data"].toArray();
        for (const auto &item : arr) {
            QJsonObject obj = item.toObject();
            QString ver = obj["version"].toString();
            if (ver.isEmpty()) ver = obj["displayName"].toString();
            if (!ver.isEmpty() && !versions.contains(ver))
                versions << ver;
        }
    }

    return versions;
}

void InstallManager::fetchLoaderVersions(const QString &mcVersion, const QString &loader)
{
    mc_info("[Install] Fetching versions for %s + %s", loader.toUtf8().constData(), mcVersion.toUtf8().constData());

    // Run on a background thread to keep UI responsive
    QThread *thread = new QThread;
    QObject *worker = new QObject;
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, [this, mcVersion, loader, thread]() {
        QStringList versions = fetchLoaderVersionsSync(mcVersion, loader);
        mc_info("[Install] Found %d versions for %s", versions.size(), loader.toUtf8().constData());
        QMetaObject::invokeMethod(this, [this, versions, loader]() {
            emit loaderVersionsReady(versions, loader);
        }, Qt::QueuedConnection);
        thread->quit();
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

#include "InstallManager.moc"
