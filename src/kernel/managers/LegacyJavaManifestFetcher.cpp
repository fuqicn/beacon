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
#include "LegacyJavaManifestFetcher.h"
#include <mc_http.h>
#include <mc_log.h>
#include <mc_download.h>
#include <mc_version.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

// The new kernel removed mc_http_release_thread_resources() (TLS QNAM is now a
// raw pointer, teardown no longer blocks). Provide an empty stub here so the
// legacy fetcher compiles against both old and new kernels.
#ifndef mc_http_release_thread_resources
#define mc_http_release_thread_resources() ((void)0)
#endif

// Parse a Java runtime manifest JSON and populate list. Shared between the
// sequential kernel path and the legacy concurrent fetcher below.
static int parse_java_manifest(const char *raw_json, const char *win_name,
                               int major_version, const char *mirror,
                               McJavaFileList *list)
{
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(raw_json), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        mc_error("Failed to parse Java manifest JSON (%s)", win_name ? win_name : "unknown");
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

    // Adoptium bundle fallback
    if (platVal.isUndefined()) {
        QJsonValue dl = root.value("download_link");
        if (!dl.toString().isEmpty()) {
            const char *bundle_url = dl.toString().toUtf8().constData();
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
        QJsonValue releases = root.value("releases");
        if (releases.isArray() && !releases.toArray().isEmpty()) {
            QJsonObject release = releases.toArray().first().toObject();
            QJsonValue dl2 = release.value("download_link");
            if (!dl2.toString().isEmpty()) {
                const char *bundle_url = dl2.toString().toUtf8().constData();
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
    HttpClient client;
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

    if (file_list.empty()) {
        mc_error("No downloadable files in Java manifest");
        return 0;
    }

    list->count = (int)file_list.size();
    list->capacity = list->count;
    list->files = (McJavaFile *)malloc(sizeof(McJavaFile) * list->count);
    if (!list->files) {
        list->count = 0;
        return 0;
    }
    for (int i = 0; i < list->count; i++)
        list->files[i] = file_list[i];

    mc_info("Java manifest: %d files for %s", list->count, version_key);
    return 1;
}

// ---------------------------------------------------------------------------
// Legacy concurrent manifest fetch
// ---------------------------------------------------------------------------
int mc_java_download_manifest_legacy(int major_version, const char *mirror, McJavaFileList *list)
{
    memset(list, 0, sizeof(*list));

    struct Source { const char *url; const char *name; };
    std::vector<Source> sources;
    sources.push_back({"https://piston-meta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json", "mojang"});
    if (mirror && strcmp(mirror, "mojang") != 0 && strcmp(mirror, "auto") != 0) {
        sources.push_back({"https://bmclapi2.bangbang93.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json", "bmclapi"});
    }
    {
        char plat[64];
        mc_platform_adoptium_name(plat, sizeof(plat));
        char adoptium_url[1024];
        snprintf(adoptium_url, sizeof(adoptium_url),
                 "https://api.adoptium.net/v3/binary/latest/%d/ga/%s?architecture=%s&os=%s&project=jdk",
                 major_version, plat, mc_arch_adoptium(mc_platform_arch_get()), mc_platform_get());
        sources.push_back({adoptium_url, "adoptium"});
    }

    std::mutex result_mtx;
    std::condition_variable result_cv;
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    std::atomic<int> active{(int)sources.size()};
    McHttpResponse *win_resp = nullptr;
    const char *win_name = nullptr;

    auto try_source = [&](const Source &src) {
        HttpClient client;
        mc_http_init(&client);
        mc_http_set_timeout(&client, 5000);
        McHttpResponse *resp = mc_http_get(&client, src.url);
        bool ok = resp && resp->success && resp->data;
        if (ok && !cancelled.exchange(true)) {
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
        mc_http_release_thread_resources();
        if (active.fetch_sub(1) == 1)
            result_cv.notify_all();
    };

    std::vector<std::thread> threads;
    threads.reserve(sources.size());
    for (auto &src : sources)
        threads.emplace_back(try_source, std::cref(src));

    {
        std::unique_lock<std::mutex> lk(result_mtx);
        result_cv.wait(lk, [&] { return done.load() || active.load() == 0; });
    }

    for (auto &t : threads)
        if (t.joinable())
            t.join();

    if (!win_resp) {
        mc_error("Failed to fetch Java runtime manifest from any source (legacy)");
        return 0;
    }
    mc_info("Java manifest from: %s (legacy)", win_name ? win_name : "unknown");

    bool parsed = parse_java_manifest(
        win_resp->data, win_name, major_version, mirror, list);
    mc_http_response_free(win_resp);
    return parsed ? 1 : 0;
}
