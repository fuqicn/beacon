/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_java_dl.h"
#include <mc_http.h>
#include <mc_log.h>
#include <mc_version.h>
#include <mc_download.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <mc_str.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

// ---- helpers ----

static QString safe_string(const QJsonObject &obj, const char *key) {
    return obj.value(key).toString();
}

static long long safe_int64(const QJsonObject &obj, const char *key) {
    return (long long)obj.value(key).toDouble(0);
}

// Fetch a single Foojay Disco ID record and return its direct_download_uri + metadata.
// Returns true and fills out_params on success; returns false on any failure.
static bool fetch_disco_id(const char *id,
                           char *out_url,   size_t url_size,
                           char *out_filename, size_t fname_size,
                           char *out_sha1,   size_t sha1_size,
                           long long *out_size) {
    QString url = QString("https://api.foojay.io/disco/v3.0/ids/%1")
                      .arg(QString::fromUtf8(id));
    HttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 8000);
    McHttpResponse *resp = mc_http_get(&client, url.toUtf8().constData());
    if (!resp || !resp->success || resp->status_code != 200 || !resp->data) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return 0;

    // Response shape: {"result": [{...}], "message": ""}
    QJsonObject pkg = doc.object().value("result").toArray().first().toObject();
    if (pkg.isEmpty()) return 0;

    QString dlUri = safe_string(pkg, "direct_download_uri");
    if (dlUri.isNull() || dlUri.isEmpty()) return 0;

    strncpy(out_url,      dlUri.toUtf8().constData(),      url_size     - 1);
    out_url[url_size     - 1] = '\0';
    strncpy(out_filename, safe_string(pkg, "filename").toUtf8().constData(), fname_size - 1);
    out_filename[fname_size - 1] = '\0';
    strncpy(out_sha1,     safe_string(pkg, "checksum").toUtf8().constData(), sha1_size  - 1);
    out_sha1[sha1_size  - 1] = '\0';
    *out_size = safe_int64(pkg, "size");
    return 1;
}

// ---- source: Mojang / BMCLAPI manifest ----
// Covers Windows only; falls through to disco when no entry for this platform.
static const char *mojang_java_manifest_url(const char *mirror) {
    if (mirror && strcmp(mirror, "bmclapi") == 0)
        return "https://bmclapi2.bangbang93.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
    return "https://piston-meta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
}

static int parse_mojang_manifest(int major_version,
                                 const char *mirror,
                                 const char *json_data,
                                 McJavaFileList *list) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_data), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        mc_error("Failed to parse Java manifest JSON");
        return 0;
    }
    QJsonObject root = doc.object();

    const char *version_key = "jre-legacy";
    if (major_version >= 22) version_key = "java-runtime-epsilon";
    else if (major_version >= 21) version_key = "java-runtime-delta";
    else if (major_version >= 17) version_key = "java-runtime-beta";
    else if (major_version >= 16) version_key = "java-runtime-alpha";

    const char *plat = mc_platform_get();
    const char *arch = mc_platform_arch_get();
    // Map our internal os/arch to Mojang manifest key names.
    // Mojang uses "mac-os"/"mac-os-arm64" (not "osx-x64"), "linux" (not "linux-x64"), etc.
    static char plat_key[32];
    if (strcmp(plat, "windows") == 0) {
        if (strcmp(arch, "x86") == 0)       snprintf(plat_key, sizeof(plat_key), "windows-x86");
        else if (strcmp(arch, "arm64") == 0) snprintf(plat_key, sizeof(plat_key), "windows-arm64");
        else                                 snprintf(plat_key, sizeof(plat_key), "windows-x64");
    } else if (strcmp(plat, "osx") == 0) {
        if (strcmp(arch, "arm64") == 0)      snprintf(plat_key, sizeof(plat_key), "mac-os-arm64");
        else                                 snprintf(plat_key, sizeof(plat_key), "mac-os");
    } else if (strcmp(plat, "linux") == 0) {
        if (strcmp(arch, "x86") == 0)        snprintf(plat_key, sizeof(plat_key), "linux-i386");
        else                                 snprintf(plat_key, sizeof(plat_key), "linux");
    } else {
        snprintf(plat_key, sizeof(plat_key), "%s-%s", plat, arch);
    }
    QJsonValue platVal = root.value(plat_key);
    if (platVal.isUndefined()) {
        snprintf(plat_key, sizeof(plat_key), "%s-x64", plat);
        platVal = root.value(plat_key);
    }
    if (platVal.isUndefined()) return 0;

    QJsonValue rtVal = platVal.toObject().value(version_key);
    if (rtVal.isUndefined() || !rtVal.isArray()) {
        if (platVal.isObject()) {
            QJsonObject platObj = platVal.toObject();
            for (auto it = platObj.begin(); it != platObj.end(); ++it) {
                rtVal = it.value();
                version_key = it.key().toUtf8().constData();
                break;
            }
        }
    }
    if (rtVal.isUndefined() || !rtVal.isArray()) {
        mc_error("No Java runtime '%s' found in manifest for %s-%s", version_key, plat, arch);
        return 0;
    }
    QJsonArray rtArr = rtVal.toArray();
    if (rtArr.isEmpty()) { mc_error("Empty Java runtime array"); return 0; }
    QJsonObject rtEntryObj = rtArr.first().toObject();
    QString manifestUrl = safe_string(rtEntryObj.value("manifest").toObject(), "url");
    if (manifestUrl.isNull() || manifestUrl.isEmpty()) {
        mc_error("No manifest URL in Java runtime entry");
        return 0;
    }

    char manifest_url[2048];
    strncpy(manifest_url, manifestUrl.toUtf8().constData(), sizeof(manifest_url) - 1);
    manifest_url[sizeof(manifest_url) - 1] = '\0';
    if (mirror && strcmp(mirror, "mojang") != 0) {
        char translated[2048];
        if (mc_download_translate_mojang_url(manifest_url, translated, sizeof(translated), mirror))
            strncpy(manifest_url, translated, sizeof(manifest_url) - 1);
    }

    HttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, manifest_url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch Java runtime file manifest");
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError fileParseErr;
    QJsonDocument fileDoc = QJsonDocument::fromJson(QByteArray(resp->data), &fileParseErr);
    mc_http_response_free(resp);
    if (fileParseErr.error != QJsonParseError::NoError || !fileDoc.isObject()) {
        mc_error("Failed to parse file manifest JSON");
        return 0;
    }
    QJsonObject fileRoot = fileDoc.object();
    QJsonValue filesVal = fileRoot.value("files");
    if (filesVal.isUndefined() || !filesVal.isObject()) {
        mc_error("No files in Java runtime manifest");
        return 0;
    }

    std::vector<McJavaFile> file_list;
    QJsonObject filesObj = filesVal.toObject();
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        const char *rel_path = it.key().toUtf8().constData();
        QJsonObject rawObj = it.value().toObject().value("downloads").toObject().value("raw").toObject();
        if (rawObj.isEmpty()) continue;
        QString dlUrl  = safe_string(rawObj, "url");
        QString dlSha1 = safe_string(rawObj, "sha1");
        long dlSize    = (long)rawObj.value("size").toDouble(0);
        if (dlUrl.isNull() || dlSha1.isNull()) continue;

        McJavaFile f{};
        strncpy(f.url,   dlUrl.toUtf8().constData(),   sizeof(f.url)   - 1);
        strncpy(f.path,  rel_path,                     sizeof(f.path)  - 1);
        strncpy(f.sha1,  dlSha1.toUtf8().constData(),  sizeof(f.sha1)  - 1);
        f.size = dlSize;
        file_list.push_back(f);
    }
    if (file_list.empty()) { mc_error("No downloadable files in Java manifest"); return 0; }

    list->count = (int)file_list.size();
    list->capacity = list->count;
    list->files = (McJavaFile *)malloc(sizeof(McJavaFile) * list->count);
    if (!list->files) { list->count = 0; return 0; }
    for (int i = 0; i < list->count; i++)
        list->files[i] = file_list[i];

    mc_info("Java manifest: %d files from mojang%s", list->count,
            (mirror && mirror[0] && strcmp(mirror, "mojang") != 0)
                ? QString(" (mirror=%1)").arg(mirror).toUtf8().constData() : "");
    return 1;
}

// ---- source: Foojay Disco API ----
// Free, keyless Java distribution discovery.  Replaces the now-dead Adoptium v3 binary endpoint.
// Flow:
//   1. GET /disco/v3.0/packages?...   → list of matching packages (client-side filter needed)
//   2. GET /disco/v3.0/ids/{id}       → actual direct_download_uri, checksum, size
// See: https://github.com/foojayio/discoapi
static const char *DISCO_API_ROOT = "https://api.foojay.io/disco/v3.0";

static int fetch_disco_java(int major_version, McJavaFileList *list) {
    const char *plat    = mc_platform_get();
    const char *arch    = mc_platform_arch_get();
    // Foojay OS names differ from ours: "macos" not "osx"/"darwin".
    const char *disco_os = (strcmp(plat, "windows") == 0) ? "windows"
                         : (strcmp(plat, "linux")   == 0) ? "linux"
                         : "macos";
    const char *disco_arch = (strcmp(arch, "x64")  == 0) ? "x64"
                           : (strcmp(arch, "arm64") == 0) ? "aarch64"
                           : (strcmp(arch, "x86")  == 0) ? "x86"
                                                         : arch;
    const char *archive_type = (strcmp(plat, "windows") == 0) ? "zip" : "tar.gz";
    // Linux needs lib_c_type=glibc to avoid Alpine/musl results.
    QString extra = (strcmp(plat, "linux") == 0) ? "&lib_c_type=glibc" : QString();

    QString url = QString("%1/packages?distribution=temurin"
                          "&java_version=%2"
                          "&os=%3"
                          "&architecture=%4"
                          "&archive_type=%5"
                          "&directly_downloadable=true%6")
                      .arg(DISCO_API_ROOT,
                           QString::number(major_version),
                           disco_os,
                           disco_arch,
                           archive_type,
                           extra);
    mc_info("Java disco: %s", qPrintable(url));

    HttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 10000);
    McHttpResponse *resp = mc_http_get(&client, url.toUtf8().constData());
    if (!resp || !resp->success || resp->status_code != 200 || !resp->data) {
        mc_warn("Java disco: query failed (code=%ld)", resp ? resp->status_code : -1);
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        mc_error("Java disco: bad JSON response");
        return 0;
    }

    // Client-side filter — server OS filter is unreliable.
    std::vector<QString> ids;
    for (auto val : doc.object().value("result").toArray()) {
        QJsonObject pkg = val.toObject();
        if (pkg.value("operating_system").toString() != disco_os) continue;
        if (pkg.value("architecture").toString()    != disco_arch) continue;
        if (pkg.value("major_version").toInt(-1)    != major_version) continue;
        if (!pkg.value("directly_downloadable").toBool(false)) continue;
        QString id = safe_string(pkg, "id");
        if (!id.isEmpty()) ids.push_back(id);
    }
    if (ids.empty()) {
        mc_warn("Java disco: no matching package for %s-%s jdk%d", plat, arch, major_version);
        return 0;
    }

    // Resolve each candidate to its direct_download_uri via the /ids/{id} endpoint.
    // The first one that resolves successfully is used.
    std::vector<McJavaFile> file_list;
    for (const auto &id : ids) {
        char url_buf[2048]  = {0};
        char fname[1024]    = {0};
        char sha1_buf[64]   = {0};
        long long sz = 0;
        if (!fetch_disco_id(id.toUtf8().constData(),
                            url_buf, sizeof(url_buf),
                            fname,   sizeof(fname),
                            sha1_buf, sizeof(sha1_buf),
                            &sz)) {
            mc_debug("Java disco: failed to resolve id=%s", qPrintable(id));
            continue;
        }
        McJavaFile f{};
        strncpy(f.url,   url_buf,    sizeof(f.url)    - 1);
        strncpy(f.path,  fname,      sizeof(f.path)   - 1);
        strncpy(f.sha1,  sha1_buf,   sizeof(f.sha1)   - 1);
        f.size = (long)sz;
        file_list.push_back(f);
        break;
    }

    if (file_list.empty()) {
        mc_warn("Java disco: all %d candidate ids resolved to empty URLs", (int)ids.size());
        return 0;
    }

    list->count = (int)file_list.size();
    list->capacity = list->count;
    list->files = (McJavaFile *)malloc(sizeof(McJavaFile) * list->count);
    if (!list->files) { list->count = 0; return 0; }
    for (int i = 0; i < list->count; i++)
        list->files[i] = file_list[i];

    mc_info("Java disco: got %d file(s) for %s-%s jdk%d (%s)",
            list->count, plat, arch, major_version, file_list[0].path);
    return 1;
}

// Fetch Java runtime file list.
// Sources tried in order:
//   1. Mojang official manifest (always included)
//   2. BMCLAPI mirror (if explicitly configured; may lack macOS/Linux entries)
//   3. Foojay Disco API — free, keyless fallback for any platform
int mc_java_download_manifest(int major_version, const char *mirror, McJavaFileList *list) {
    memset(list, 0, sizeof(*list));

    // Always try Mojang directly as the canonical source (works on all platforms).
    // BMCLAPI sometimes lacks macOS/Linux Java entries, so we also try it separately.
    const char *urls[2];
    int n_urls = 0;
    urls[n_urls++] = mojang_java_manifest_url(nullptr);  // always mojang
    if (mirror && strcmp(mirror, "mojang") != 0 && strcmp(mirror, "auto") != 0) {
        urls[n_urls++] = mojang_java_manifest_url(mirror);
    }

    for (int i = 0; i < n_urls; i++) {
        const char *url = urls[i];
        HttpClient client;
        mc_http_init(&client);
        mc_http_set_timeout(&client, 5000);
        mc_info("Java manifest: trying %s ...", url);
        McHttpResponse *resp = mc_http_get(&client, url);
        bool ok = resp && resp->success && resp->data;
        if (ok) {
            int parsed = parse_mojang_manifest(major_version, mirror, resp->data, list);
            mc_http_response_free(resp);
            if (parsed) return 1;
            mc_warn("Java manifest: no entry for %s-%s in %s",
                    mc_platform_get(), mc_platform_arch_get(), url);
        } else {
            if (resp) mc_http_response_free(resp);
            mc_warn("Java manifest: %s unavailable", url);
        }
    }

    // Fallback: Foojay Disco API — free, keyless, covers Windows/macOS/Linux
    mc_info("Java manifest: falling back to disco api ...");
    if (fetch_disco_java(major_version, list)) return 1;

    mc_error("Failed to get Java runtime file list");
    return 0;
}

void mc_java_file_list_free(McJavaFileList *list) {
    if (list && list->files) {
        free(list->files);
        list->files = nullptr;
    }
    list->count = 0;
    list->capacity = 0;
}
