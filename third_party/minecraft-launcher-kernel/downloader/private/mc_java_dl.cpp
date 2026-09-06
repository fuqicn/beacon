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
#include <mc_download.h>
#include <mc_version.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include <QtCore/QDir>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>

// Mojang Java runtime manifest URL for the legacy-2ec0cc96 reference build.
static const char *mojang_java_manifest_url(const char *mirror) {
    if (mirror && strcmp(mirror, "bmclapi") == 0)
        return "https://bmclapi2.bangbang93.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
    return "https://piston-meta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
}

// Eclipse Adoptium API URL for the latest GA release of a given platform+arch.
// Returns the caller-allocated string (stack buffer in this function, safe
// because it is only used synchronously before the function returns).
static void build_adoptium_url(int major_ver, char *out, size_t out_size) {
    char plat[64];
    mc_platform_adoptium_name(plat, sizeof(plat));
    snprintf(out, out_size,
             "https://api.adoptium.net/v3/binary/latest/%d/ga/%s/linux_x64?project=jdk",
             major_ver, plat);
    // The Adoptium API redirects to the actual download URL; we follow with mc_http_get.
    (void)mc_download_translate_url(out, out, out_size, "mojang");
}

// Concurrently fetch Java runtime manifest from all available sources and
// return the first successful result. Sources are tried in parallel with a
// 5s timeout each so a slow mirror never blocks the fast one.
int mc_java_download_manifest(int major_version, const char *mirror, McJavaFileList *list) {
    memset(list, 0, sizeof(*list));

    // Build URLs for all candidate sources
    struct Source { const char *url; const char *name; };
    std::vector<Source> sources;
    sources.push_back({"https://piston-meta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json", "mojang"});
    if (mirror && strcmp(mirror, "mojang") != 0 && strcmp(mirror, "auto") != 0) {
        sources.push_back({"https://bmclapi2.bangbang93.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json", "bmclapi"});
    }
    // Always add Adoptium as a fallback source
    {
        char plat[64];
        mc_platform_adoptium_name(plat, sizeof(plat));
        char adoptium_url[1024];
        snprintf(adoptium_url, sizeof(adoptium_url),
                 "https://api.adoptium.net/v3/binary/latest/%d/ga/%s?architecture=%s&os=%s&project=jdk",
                 major_version, plat, mc_arch_adoptium(mc_platform_arch_get()), mc_platform_get());
        sources.push_back({adoptium_url, "adoptium"});
    }

    // Concurrent fetch: each source runs in its own thread.
    // First successful result wins.
    std::mutex result_mtx;
    std::condition_variable result_cv;
    std::atomic<bool> done{false};
    std::atomic<int> active{0};
    McHttpResponse *win_resp = nullptr;
    const char *win_name = nullptr;

    auto try_source = [&](const Source &src) {
        active.fetch_add(1);
        McHttpClient client;
        mc_http_init(&client);
        mc_http_set_timeout(&client, 5000);
        McHttpResponse *resp = mc_http_get(&client, src.url);
        bool ok = resp && resp->success && resp->data;
        if (ok) {
            std::lock_guard<std::mutex> lk(result_mtx);
            if (!done.exchange(true)) {
                win_resp = resp;
                win_name = src.name;
                result_cv.notify_all();
            } else {
                mc_http_response_free(resp);
            }
        } else {
            if (resp) mc_http_response_free(resp);
        }
        active.fetch_sub(1);
    };

    std::vector<std::thread> threads;
    threads.reserve(sources.size());
    for (auto &src : sources)
        threads.emplace_back(try_source, std::cref(src));

    // Wait for first success or all failures
    {
        std::unique_lock<std::mutex> lk(result_mtx);
        result_cv.wait(lk, [&] { return done.load(); });
    }
    for (auto &t : threads) t.join();

    if (!win_resp) {
        mc_error("Failed to fetch Java runtime manifest from any source");
        return 0;
    }
    mc_info("Java manifest from: %s", win_name ? win_name : "unknown");

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(win_resp->data), &parseErr);
    mc_http_response_free(win_resp);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
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
    char plat_key[32];
    snprintf(plat_key, sizeof(plat_key), "%s-%s", plat, arch);
    QJsonValue platVal = root.value(plat_key);
    if (platVal.isUndefined()) {
        snprintf(plat_key, sizeof(plat_key), "%s-x64", plat);
        platVal = root.value(plat_key);
    }

    // Adoptium bundle fallback (single binary download)
    if (platVal.isUndefined()) {
        mc_warn("Java manifest: no entry for %s-%s, checking Adoptium bundle", plat, arch);
        // The Adoptium source already returned a bundle descriptor if it was the winner.
        // Check if the root has "download_link" directly (bundle format).
        QJsonValue dl = root.value("download_link");
        if (!dl.toString().isEmpty()) {
            const char *bundle_url = dl.toString().toUtf8().constData();
            mc_info("Adoptium bundle URL: %s", bundle_url);
            list->count = 1;
            list->capacity = 1;
            list->files = (McJavaFile *)malloc(sizeof(McJavaFile));
            if (list->files) {
                memset(list->files, 0, sizeof(McJavaFile));
                strncpy(list->files[0].url, bundle_url, sizeof(list->files[0].url) - 1);
                strncpy(list->files[0].path, "adoptium-bundle", sizeof(list->files[0].path) - 1);
                list->files[0].size = 0;
            }
            mc_info("Java manifest (Adoptium bundle): 1 file for %s-%s", plat, arch);
            return 1;
        }
        // Also try the multi-source manifest path for Adoptium
        QJsonValue releases = root.value("releases");
        if (releases.isArray() && !releases.toArray().isEmpty()) {
            QJsonObject release = releases.toArray().first().toObject();
            QJsonValue dl2 = release.value("download_link");
            if (!dl2.toString().isEmpty()) {
                const char *bundle_url = dl2.toString().toUtf8().constData();
                mc_info("Adoptium bundle URL: %s", bundle_url);
                list->count = 1;
                list->capacity = 1;
                list->files = (McJavaFile *)malloc(sizeof(McJavaFile));
                if (list->files) {
                    memset(list->files, 0, sizeof(McJavaFile));
                    strncpy(list->files[0].url, bundle_url, sizeof(list->files[0].url) - 1);
                    strncpy(list->files[0].path, "adoptium-bundle", sizeof(list->files[0].path) - 1);
                    list->files[0].size = 0;
                }
                return 1;
            }
        }
        mc_warn("Java manifest: no bundle available for %s-%s", plat, arch);
        return 0;
    }

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
        mc_error("No Java runtime '%s' found in manifest", version_key);
        return 0;
    }
    QJsonArray rtArr = rtVal.toArray();

    QJsonValue rtEntryVal = rtArr.at(0);
    if (rtEntryVal.isUndefined()) {
        mc_error("Empty Java runtime array");
        return 0;
    }
    QJsonObject rtEntryObj = rtEntryVal.toObject();

    QJsonValue manifestVal = rtEntryObj.value("manifest");
    if (manifestVal.isUndefined()) {
        mc_error("No manifest in Java runtime entry");
        return 0;
    }
    QJsonObject manifestObj = manifestVal.toObject();
    QString manifestUrl = manifestObj.value("url").toString();
    if (manifestUrl.isNull()) {
        mc_error("No manifest URL in Java runtime entry");
        return 0;
    }

    char manifest_url_translated[2048];
    QByteArray manifestUrlUtf8 = manifestUrl.toUtf8();
    strncpy(manifest_url_translated, manifestUrlUtf8.constData(), sizeof(manifest_url_translated) - 1);
    manifest_url_translated[sizeof(manifest_url_translated) - 1] = '\0';
    if (mirror && strcmp(mirror, "mojang") != 0) {
        char translated[2048];
        if (mc_download_translate_mojang_url(manifestUrlUtf8.constData(), translated, sizeof(translated), mirror))
            strncpy(manifest_url_translated, translated, sizeof(manifest_url_translated) - 1);
    }
    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, manifest_url_translated);
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
    QJsonObject filesObj = filesVal.toObject();

    std::vector<McJavaFile> file_list;
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        QByteArray relPathBa = it.key().toUtf8();
        const char *rel_path = relPathBa.constData();

        QJsonValue entryVal = it.value();
        if (!entryVal.isObject()) continue;

        QJsonObject entryObj = entryVal.toObject();
        QJsonValue downloadsVal = entryObj.value("downloads");
        if (downloadsVal.isUndefined()) continue;
        QJsonValue rawVal = downloadsVal.toObject().value("raw");
        if (rawVal.isUndefined()) continue;

        QJsonObject rawObj = rawVal.toObject();
        QString dlUrl = rawObj.value("url").toString();
        QString dlSha1 = rawObj.value("sha1").toString();
        long dlSize = (long)rawObj.value("size").toDouble(0);

        if (dlUrl.isNull() || dlSha1.isNull()) continue;

        QByteArray dlUrlBa = dlUrl.toUtf8();
        QByteArray dlSha1Ba = dlSha1.toUtf8();

        McJavaFile f;
        memset(&f, 0, sizeof(f));
        strncpy(f.url, dlUrlBa.constData(), sizeof(f.url) - 1);
        strncpy(f.path, rel_path, sizeof(f.path) - 1);
        strncpy(f.sha1, dlSha1Ba.constData(), sizeof(f.sha1) - 1);
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

    mc_info("Java manifest: %d files for %s", list->count, version_key);
    return 1;
}

void mc_java_file_list_free(McJavaFileList *list) {
    if (list && list->files) {
        free(list->files);
        list->files = nullptr;
    }
    list->count = 0;
    list->capacity = 0;
}
