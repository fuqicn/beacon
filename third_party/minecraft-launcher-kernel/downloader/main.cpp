/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mcbase.h>
#include "private/include/mc_asset.h"
#include "private/include/mc_java_dl.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <functional>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

static McVersion *new_version(void) {
    McVersion *v = static_cast<McVersion*>(malloc(sizeof(McVersion)));
    if (v) mc_version_init(v);
    return v;
}

static void del_version(McVersion *v) {
    if (v) { mc_version_free(v); free(v); }
}

static const char *g_mirror = nullptr;

static void probe_and_select_mirror(const char **mirror_type) {
    if (!mirror_type || !*mirror_type) return;
    if (strcmp(*mirror_type, "auto") != 0) return;
    mc_info("Probing mirrors...");
    McMirrorProbe probes[MC_MAX_MIRROR_TYPES];
    int n = mc_mirror_probe_all(probes, MC_MAX_MIRROR_TYPES);
    for (int i = 0; i < n; i++) {
        const char *status = probes[i].available ? "OK" : "DOWN";
        mc_info("  %s: %s (%.0f ms)", probes[i].mirror_type, status, probes[i].latency_ms);
    }
    const char *best = mc_mirror_select_best(probes, n);
    if (strcmp(best, *mirror_type) != 0) {
        mc_info("Auto-selected mirror: %s", best);
        *mirror_type = best;
    }
}

static void translate_url(const char *src, char *dst, size_t dst_size) {
    if (g_mirror && strcmp(g_mirror, "mojang") != 0) {
        if (!mc_download_translate_mojang_url(src, dst, static_cast<int>(dst_size), g_mirror))
            strncpy(dst, src, dst_size - 1);
    } else {
        strncpy(dst, src, dst_size - 1);
    }
}

static int fetch_version(McVersion *v, const char *id) {
    if (g_mirror)
        return mc_version_fetch_by_id_mirror(v, id, g_mirror);
    return mc_version_fetch_by_id(v, id);
}

static int fetch_version_or_local(McVersion *v, const char *id, const char *mc_dir) {
    if (fetch_version(v, id)) return 1;
    char local_path[MC_PATH_MAX];
    snprintf(local_path, sizeof(local_path), "%s/versions/%s/%s.json", mc_dir, id, id);
    if (mc_path_exists(local_path) && mc_version_parse_file(v, local_path)) {
        mc_info("Loaded local version JSON: %s", local_path);
        return 1;
    }
    return 0;
}

class DownloadPool {
    int m_max_threads;
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    bool m_stop = false;
    std::atomic<int> m_pending{0};

    void worker() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mtx);
                m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
                if (m_stop && m_tasks.empty()) return;
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            task();
            m_pending--;
            m_cv.notify_all();
        }
    }

public:
    DownloadPool(int threads) : m_max_threads(threads > 0 ? threads : 16) {}

    ~DownloadPool() {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto &w : m_workers)
            if (w.joinable()) w.join();
    }

    int thread_count() const { return m_max_threads; }

    void enqueue(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_tasks.push(std::move(task));
        m_pending++;
        if (m_workers.size() < (size_t)m_max_threads)
            m_workers.emplace_back(&DownloadPool::worker, this);
        m_cv.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(m_mtx);
        while (m_pending != 0) {
            if (m_cv.wait_for(lock, std::chrono::milliseconds(50)) == std::cv_status::timeout) {
                lock.unlock();
                if (QCoreApplication::instance())
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                lock.lock();
            }
        }
    }
};

struct WorkItem {
    std::string url;
    std::string fallback_url;
    std::string output_path;
    std::string expected_sha1;
    long expected_size = 0;
    int success = 0;
};

static long g_timeout_ms = 120000;

static void print_help(void) {
    std::ostringstream oss;
    oss << "downloader - " << mc_i18n("downloader_desc") << std::endl;
    oss << mc_i18n("usage") << ": downloader <" << mc_i18n("commands") << "> [" << mc_i18n("options") << "]" << std::endl;
    oss << std::endl;
    oss << mc_i18n("commands") << ":" << std::endl;
    oss << "  java <ver>              " << mc_i18n("download_java") << std::endl;
    oss << "  mc <version-id>         " << mc_i18n("download_mc") << std::endl;
    oss << "  client <version-id>     " << mc_i18n("download_client") << std::endl;
    oss << "  server <version-id>     " << mc_i18n("download_server") << std::endl;
    oss << "  libraries <version-id>  " << mc_i18n("download_libraries") << std::endl;
    oss << "  natives <version-id>    " << mc_i18n("download_natives") << std::endl;
    oss << "  assets <version-id>     " << mc_i18n("download_assets") << std::endl;
    oss << "  logging <version-id>    " << mc_i18n("download_logging") << std::endl;
    oss << "  mod <url>               " << mc_i18n("mod_download") << std::endl;
    oss << "  url <url> --output <path> " << mc_i18n("download_url") << std::endl;
    oss << "  help                    " << mc_i18n("show_help") << std::endl;
    oss << std::endl;
    oss << mc_i18n("options") << ":" << std::endl;
    oss << "  --dir <path>     " << mc_i18n("output_dir") << std::endl;
    oss << "  --mirror <type>  " << mc_i18n("mirror") << std::endl;
    oss << "  --threads <n>    " << mc_i18n("threads") << std::endl;
    oss << "  --platform <os>  " << mc_i18n("platform_opt") << std::endl;
    oss << "  --lang <code>    " << mc_i18n("lang_opt") << std::endl;
    oss << "  --json           " << mc_i18n("json_opt") << std::endl;
    oss << "  --debug          " << mc_i18n("debug_opt") << std::endl;
    oss << std::endl;
    mc_console_write(oss.str().c_str());
}

static int download_file_with_progress(const char *url, const char *output_path,
    const char *sha1, long size, const char *label)
{
    const char *fname = strrchr(output_path, '/');
    if (!fname) fname = strrchr(output_path, '\\');
    if (fname) fname++; else fname = output_path;
    mc_info("[%s] %s...", label, fname);
    int r = mc_qt_download_file(url, output_path, sha1, size, g_timeout_ms);
    if (r)
        mc_info("[%s] %s: OK", label, fname);
    else
        mc_warn("[%s] %s: %s", label, fname, mc_i18n("download_failed"));
    return r;
}

static void run_works(DownloadPool &pool, std::vector<WorkItem> &works, const char *label) {
    int total = static_cast<int>(works.size());
    mc_info("%s: %d %s", label, total, mc_i18n("files"));
    if (total == 0) return;

    // Pre-translate URLs
    std::vector<std::string> url_strs(total);
    std::vector<const char*> urls(total);
    std::vector<const char*> paths(total);
    std::vector<const char*> sha1s(total);
    std::vector<long> sizes(total);
    std::vector<int> results(total);
    for (int i = 0; i < total; i++) {
        auto &w = works[i];
        char buf[2048];
        translate_url(w.url.c_str(), buf, sizeof(buf));
        url_strs[i] = buf;
        urls[i] = url_strs[i].c_str();
        paths[i] = w.output_path.c_str();
        sha1s[i] = w.expected_sha1.empty() ? nullptr : w.expected_sha1.c_str();
        sizes[i] = w.expected_size;
    }

    // Batch-submit ALL files at once so process_all starts ALL concurrently
    int ok = mc_qt_download_batch(urls.data(), paths.data(), sha1s.data(),
                                   sizes.data(), total, g_timeout_ms, results.data());

    // Retry failed files with fallback URL (if available)
    for (int i = 0; i < total; i++) {
        auto &w = works[i];
        const char *fname = strrchr(w.output_path.c_str(), '/');
        if (!fname) fname = strrchr(w.output_path.c_str(), '\\');
        if (fname) fname++; else fname = w.output_path.c_str();

        if (results[i]) {
            w.success = 1;
        } else if (!w.fallback_url.empty()) {
            mc_info("[%s] %s: retrying fallback...", label, fname);
            w.success = mc_qt_download_file(w.fallback_url.c_str(),
                w.output_path.c_str(),
                w.expected_sha1.empty() ? nullptr : w.expected_sha1.c_str(),
                w.expected_size, g_timeout_ms);
            if (!w.success)
                mc_warn("[%s] %s: %s", label, fname, mc_i18n("download_failed"));
        } else {
            w.success = 0;
            mc_warn("[%s] %s: %s", label, fname, mc_i18n("download_failed"));
        }
    }
}

static int download_version_json(McVersion *v, const char *version_id, const char *mc_dir) {
    if (fetch_version(v, version_id)) {
        char versions_dir[MC_PATH_MAX];
        mc_path_join(mc_dir, "versions", versions_dir, sizeof(versions_dir));
        char ver_dir[MC_PATH_MAX];
        mc_path_join(versions_dir, v->id, ver_dir, sizeof(ver_dir));
        mc_path_mkdir_p(ver_dir);
        char json_path[MC_PATH_MAX];
        snprintf(json_path, sizeof(json_path), "%s/%s.json", ver_dir, v->id);
        if (!v->raw_json.isEmpty()) {
            QJsonDocument doc(v->raw_json);
            QByteArray json_bytes = doc.toJson(QJsonDocument::Indented);
            FILE *f = fopen(json_path, "wb");
            if (f) { fwrite(json_bytes.constData(), 1, json_bytes.size(), f); fclose(f); }
        }
        mc_info("Version JSON saved: %s/%s.json", ver_dir, v->id);
        return 1;
    }
    // Not in remote manifest — try loading from local file (e.g. loader profiles)
    char local_path[MC_PATH_MAX];
    snprintf(local_path, sizeof(local_path), "%s/versions/%s/%s.json", mc_dir, version_id, version_id);
    if (mc_path_exists(local_path) && mc_version_parse_file(v, local_path)) {
        mc_info("Loaded local version JSON: %s", local_path);
        return 1;
    }
    mc_error("Failed to fetch version: %s", version_id);
    return 0;
}

static int download_client_jar(McVersion *v, const char *mc_dir) {
    // If this version inherits from another, use the base version's client jar
    char ver_id[64], client_url[2048], client_sha1[64];
    long client_size = 0;
    strncpy(ver_id, v->id, sizeof(ver_id) - 1);
    strncpy(client_url, v->client_url, sizeof(client_url) - 1);
    strncpy(client_sha1, v->client_sha1, sizeof(client_sha1) - 1);
    client_size = v->client_size;
    if (!client_url[0] && v->inherits_from[0]) {
        char base_json_path[MC_PATH_MAX];
        snprintf(base_json_path, sizeof(base_json_path), "%s/versions/%s/%s.json", mc_dir, v->inherits_from, v->inherits_from);
        if (mc_path_exists(base_json_path)) {
            McVersion *base = (McVersion *)malloc(sizeof(McVersion));
            if (base) {
                mc_version_init(base);
                if (mc_version_parse_file(base, base_json_path)) {
                    strncpy(ver_id, base->id, sizeof(ver_id) - 1);
                    strncpy(client_url, base->client_url, sizeof(client_url) - 1);
                    strncpy(client_sha1, base->client_sha1, sizeof(client_sha1) - 1);
                    client_size = base->client_size;
                }
                mc_version_free(base);
                free(base);
            }
        }
    }
    if (!client_url[0]) { mc_error("No client download URL"); return 0; }

    char versions_dir[MC_PATH_MAX];
    mc_path_join(mc_dir, "versions", versions_dir, sizeof(versions_dir));
    char ver_dir[MC_PATH_MAX];
    mc_path_join(versions_dir, ver_id, ver_dir, sizeof(ver_dir));
    mc_path_mkdir_p(ver_dir);
    char jar_path[MC_PATH_MAX];
    snprintf(jar_path, sizeof(jar_path), "%s/%s.jar", ver_dir, ver_id);

    char primary[2048], fallback[2048];
    translate_url(client_url, primary, sizeof(primary));
    fallback[0] = '\0';
    if (g_mirror && strcmp(g_mirror, "mojang") != 0)
        strncpy(fallback, client_url, sizeof(fallback) - 1);

    if (mc_path_exists(jar_path)) {
        mc_info("Client JAR exists: %s", jar_path);
        return 1;
    }

    mc_info("Client JAR not found, downloading: %s", jar_path);
    int r = mc_qt_download_file(primary, jar_path,
        client_sha1[0] ? client_sha1 : nullptr,
        client_size, g_timeout_ms);
    if (r) {
        mc_info("Client JAR saved: %s", jar_path);
        return 1;
    }
    if (fallback[0]) {
        mc_info("Retrying with fallback URL...");
        r = mc_qt_download_file(fallback, jar_path,
            client_sha1[0] ? client_sha1 : nullptr,
            client_size, g_timeout_ms);
        if (r) {
            mc_info("Client JAR saved (fallback): %s", jar_path);
            return 1;
        }
    }
    mc_error("Failed to download client JAR");
    return 0;
}

static int download_server_jar(McVersion *v, const char *mc_dir) {
    if (!v->server_url[0]) { mc_error("No server download URL"); return 0; }
    char server_path[MC_PATH_MAX];
    snprintf(server_path, sizeof(server_path), "%s/server_%s.jar", mc_dir, v->id);
    char primary[2048], fallback[2048];
    translate_url(v->server_url, primary, sizeof(primary));
    fallback[0] = '\0';
    if (g_mirror && strcmp(g_mirror, "mojang") != 0)
        strncpy(fallback, v->server_url, sizeof(fallback) - 1);
    if (download_file_with_progress(primary, server_path,
        v->server_sha1[0] ? v->server_sha1 : nullptr,
        v->server_size, "server"))
    {
        mc_info("Server JAR saved: %s", server_path); return 1;
    }
    if (fallback[0] && download_file_with_progress(fallback, server_path,
        v->server_sha1[0] ? v->server_sha1 : nullptr,
        v->server_size, "server"))
    {
        mc_info("Server JAR saved (fallback): %s", server_path); return 1;
    }
    mc_error("Failed to download server JAR");
    return 0;
}

static int download_libraries(McVersion *v, const char *mc_dir, DownloadPool &pool) {
    char libraries_dir[MC_PATH_MAX];
    mc_path_join(mc_dir, "libraries", libraries_dir, sizeof(libraries_dir));

    // Process both the custom version's libraries and inherited version's libraries
    McVersion *base_v = v;
    McVersion *base_buf = nullptr;
    int has_base = 0;
    if (v->inherits_from[0]) {
        base_buf = (McVersion*)malloc(sizeof(McVersion));
        if (base_buf) mc_version_init(base_buf);
        char base_json_path[MC_PATH_MAX];
        snprintf(base_json_path, sizeof(base_json_path), "%s/versions/%s/%s.json", mc_dir, v->inherits_from, v->inherits_from);
        if (base_buf && mc_path_exists(base_json_path) && mc_version_parse_file(base_buf, base_json_path)) {
            base_v = base_buf;
            has_base = 1;
        } else {
            char url[512], translated[512];
            snprintf(url, sizeof(url), "https://piston-meta.mojang.com/v1/packages/%s/%s.json", v->inherits_from, v->inherits_from);
            translate_url(url, translated, sizeof(translated));
            McVersion *tmp = new_version();
            if (tmp && mc_version_fetch(tmp, translated)) {
                base_v = tmp;
                has_base = 2;
            } else { del_version(tmp); }
        }
    }

    std::vector<WorkItem> works;
    // First process custom version's libraries (loader-specific)
    for (int i = 0; i < v->library_count; i++) {
        McLibrary *lib = &v->libraries[i];
        if (!lib->is_required) continue;

        // For pure-native library entries (new-style, no separate classifier_url),
        // the artifact IS the native JAR, handled by the natives block below.
        // For non-natives and old-style natives (with classifier_url), download
        // the Java artifact JAR normally.
        if (!lib->is_natives || lib->classifier_url[0]) {
            char rel[MC_PATH_MAX];
            mc_library_resolve_path(lib->name, rel, sizeof(rel));
            if (!rel[0]) continue;
            char local_path[MC_PATH_MAX];
            mc_path_join(libraries_dir, rel, local_path, sizeof(local_path));
            if (mc_path_exists(local_path)) {
                char actual[64];
                if (mc_hash_file_sha1(local_path, actual, sizeof(actual)) &&
                    lib->sha1[0] && mc_stricmp(actual, lib->sha1) == 0)
                    continue;
            }
            // Build the download URL.
            // lib->url may be a full artifact URL (modern format, ends with .jar)
            // or a Maven base URL (legacy format, no .jar suffix).
            char lib_url[2048];
            size_t url_len = strlen(lib->url);
            if (url_len > 4 && memcmp(lib->url + url_len - 4, ".jar", 4) == 0) {
                strncpy(lib_url, lib->url, sizeof(lib_url) - 1);
                lib_url[sizeof(lib_url) - 1] = '\0';
            } else {
                mc_library_resolve_url(lib->name, lib->url[0] ? lib->url : nullptr, lib_url, sizeof(lib_url));
                if (!lib_url[0]) continue;
            }
            char primary[2048], fallback[2048];
            translate_url(lib_url, primary, sizeof(primary));
            fallback[0] = '\0';
            if (g_mirror && strcmp(g_mirror, "mojang") != 0)
                strncpy(fallback, lib_url, sizeof(fallback) - 1);
            mc_path_mkdir_p(libraries_dir);
            char parent[MC_PATH_MAX];
            mc_path_dirname(local_path, parent, sizeof(parent));
            mc_path_mkdir_p(parent);
            WorkItem w;
            w.url = primary;
            w.fallback_url = fallback;
            w.output_path = local_path;
            w.expected_sha1 = lib->sha1;
            w.expected_size = lib->size;
            works.push_back(std::move(w));
        }

        // Resolve natives if present (old-style with natives_key, or new-style with classifier in name)
        if (lib->is_natives && (lib->natives_key[0] || !lib->classifier_url[0])) {
            char rel[MC_PATH_MAX];
            mc_library_natives_path(lib->name, lib->natives_key, rel, sizeof(rel));
            if (!rel[0]) continue;
            char local_path[MC_PATH_MAX];
            mc_path_join(libraries_dir, rel, local_path, sizeof(local_path));
            if (mc_path_exists(local_path)) { continue; }
            char lib_url[2048];
            const char *classifier_url = lib->classifier_url[0] ? lib->classifier_url : nullptr;
            if (classifier_url) {
                strncpy(lib_url, classifier_url, sizeof(lib_url) - 1);
                lib_url[sizeof(lib_url) - 1] = '\0';
            } else {
                // For new-style native entries, lib->url is the full native JAR URL
                size_t url_len = strlen(lib->url);
                if (url_len > 4 && memcmp(lib->url + url_len - 4, ".jar", 4) == 0) {
                    strncpy(lib_url, lib->url, sizeof(lib_url) - 1);
                    lib_url[sizeof(lib_url) - 1] = '\0';
                } else {
                    mc_library_resolve_url(lib->name, lib->url[0] ? lib->url : nullptr, lib_url, sizeof(lib_url));
                    if (!lib_url[0]) continue;
                }
            }
            char primary[2048], fallback[2048];
            translate_url(lib_url, primary, sizeof(primary));
            fallback[0] = '\0';
            if (g_mirror && strcmp(g_mirror, "mojang") != 0)
                strncpy(fallback, lib_url, sizeof(fallback) - 1);
            char parent[MC_PATH_MAX];
            mc_path_dirname(local_path, parent, sizeof(parent));
            mc_path_mkdir_p(parent);
            WorkItem w;
            w.url = primary;
            w.fallback_url = fallback;
            w.output_path = local_path;
            w.expected_sha1 = lib->classifier_sha1[0] ? lib->classifier_sha1 : lib->sha1;
            w.expected_size = lib->classifier_size ? lib->classifier_size : lib->size;
            works.push_back(std::move(w));
        }
    }

    // Also process inherited version's libraries if present
    if (has_base) {
        for (int i = 0; i < base_v->library_count; i++) {
            McLibrary *lib = &base_v->libraries[i];
            if (!lib->is_required) continue;

            if (!lib->is_natives || lib->classifier_url[0]) {
                char rel[MC_PATH_MAX];
                mc_library_resolve_path(lib->name, rel, sizeof(rel));
                if (!rel[0]) continue;
                char local_path[MC_PATH_MAX];
                mc_path_join(libraries_dir, rel, local_path, sizeof(local_path));
                if (mc_path_exists(local_path)) {
                    char actual[64];
                    if (mc_hash_file_sha1(local_path, actual, sizeof(actual)) &&
                        lib->sha1[0] && mc_stricmp(actual, lib->sha1) == 0)
                        continue;
                }
                char lib_url[2048];
                size_t url_len = strlen(lib->url);
                if (url_len > 4 && memcmp(lib->url + url_len - 4, ".jar", 4) == 0) {
                    strncpy(lib_url, lib->url, sizeof(lib_url) - 1);
                    lib_url[sizeof(lib_url) - 1] = '\0';
                } else {
                    mc_library_resolve_url(lib->name, lib->url[0] ? lib->url : nullptr, lib_url, sizeof(lib_url));
                    if (!lib_url[0]) continue;
                }
                char primary[2048], fallback[2048];
                translate_url(lib_url, primary, sizeof(primary));
                fallback[0] = '\0';
                if (g_mirror && strcmp(g_mirror, "mojang") != 0)
                    strncpy(fallback, lib_url, sizeof(fallback) - 1);
                char parent[MC_PATH_MAX];
                mc_path_dirname(local_path, parent, sizeof(parent));
                mc_path_mkdir_p(parent);
                WorkItem w;
                w.url = primary;
                w.fallback_url = fallback;
                w.output_path = local_path;
                w.expected_sha1 = lib->sha1;
                w.expected_size = lib->size;
                works.push_back(std::move(w));
            }
            if (lib->is_natives && (lib->natives_key[0] || !lib->classifier_url[0])) {
                char rel[MC_PATH_MAX];
                mc_library_natives_path(lib->name, lib->natives_key, rel, sizeof(rel));
                if (!rel[0]) continue;
                char local_path[MC_PATH_MAX];
                mc_path_join(libraries_dir, rel, local_path, sizeof(local_path));
                if (mc_path_exists(local_path)) { continue; }
                char lib_url[2048];
                const char *classifier_url = lib->classifier_url[0] ? lib->classifier_url : nullptr;
                if (classifier_url) {
                    strncpy(lib_url, classifier_url, sizeof(lib_url) - 1);
                    lib_url[sizeof(lib_url) - 1] = '\0';
                } else {
                    size_t url_len = strlen(lib->url);
                    if (url_len > 4 && memcmp(lib->url + url_len - 4, ".jar", 4) == 0) {
                        strncpy(lib_url, lib->url, sizeof(lib_url) - 1);
                        lib_url[sizeof(lib_url) - 1] = '\0';
                    } else {
                        mc_library_resolve_url(lib->name, lib->url[0] ? lib->url : nullptr, lib_url, sizeof(lib_url));
                        if (!lib_url[0]) continue;
                    }
                }
                char primary[2048], fallback[2048];
                translate_url(lib_url, primary, sizeof(primary));
                fallback[0] = '\0';
                if (g_mirror && strcmp(g_mirror, "mojang") != 0)
                    strncpy(fallback, lib_url, sizeof(fallback) - 1);
                char parent[MC_PATH_MAX];
                mc_path_dirname(local_path, parent, sizeof(parent));
                mc_path_mkdir_p(parent);
                WorkItem w;
                w.url = primary;
                w.fallback_url = fallback;
                w.output_path = local_path;
                w.expected_sha1 = lib->classifier_sha1[0] ? lib->classifier_sha1 : lib->sha1;
                w.expected_size = lib->classifier_size ? lib->classifier_size : lib->size;
                works.push_back(std::move(w));
            }
        }
    }

    run_works(pool, works, mc_i18n("download_libraries"));
    int ok = 0, fail = 0, skip = static_cast<int>(works.size()) - 0;
    for (auto &w : works) { if (w.success) ok++; else fail++; }
    mc_info("Libraries: %d ok, %d skipped, %d failed", ok, skip, fail);

    if (has_base == 1) { mc_version_free(base_buf); free(base_buf); }
    else if (has_base == 2) { mc_version_free(base_v); free(base_v); }

    return fail == 0;
}

static int download_natives(McVersion *v, const char *mc_dir, DownloadPool &pool) {
    mc_info("download_natives: entering, mc_dir=%s, library_count=%d", mc_dir, v->library_count);

    // First download all native library JARs
    mc_info("download_natives: calling download_libraries...");
    if (!download_libraries(v, mc_dir, pool)) {
        mc_warn("Some native libraries failed to download");
    }
    mc_info("download_natives: download_libraries done");

    // Extract native libraries from downloaded JARs
    char libraries_dir[MC_PATH_MAX];
    mc_path_join(mc_dir, "libraries", libraries_dir, sizeof(libraries_dir));
    char natives_dir[MC_PATH_MAX];
    snprintf(natives_dir, sizeof(natives_dir), "%s/natives-%s", mc_dir, v->id);
    mc_path_mkdir_p(natives_dir);
    mc_info("download_natives: libraries_dir=%s, natives_dir=%s", libraries_dir, natives_dir);

    // Get the effective version (use inherits_from if this version has no natives of its own)
    McVersion *base_v = v;
    McVersion *base_buf = nullptr;
    int has_base = 0;
    if (v->inherits_from[0]) {
        // Check if this version has native libraries, if not use base
        int has_natives = 0;
        for (int i = 0; i < v->library_count; i++)
            if (v->libraries[i].is_natives) { has_natives = 1; break; }
        mc_info("download_natives: inherits_from=%s, has_natives=%d", v->inherits_from, has_natives);
        if (!has_natives) {
            base_buf = (McVersion*)malloc(sizeof(McVersion));
            if (base_buf) mc_version_init(base_buf);
            char base_json_path[MC_PATH_MAX];
            snprintf(base_json_path, sizeof(base_json_path), "%s/versions/%s/%s.json", mc_dir, v->inherits_from, v->inherits_from);
            if (base_buf && mc_path_exists(base_json_path) && mc_version_parse_file(base_buf, base_json_path)) {
                base_v = base_buf;
                has_base = 1;
                mc_info("download_natives: loaded base version, library_count=%d", base_v->library_count);
            }
        }
    }

    mc_info("download_natives: starting extraction loop, base_v->library_count=%d", base_v->library_count);
    int extracted = 0;
    for (int i = 0; i < base_v->library_count; i++) {
        McLibrary *lib = &base_v->libraries[i];
        mc_info("download_natives: library[%d] name=%s is_required=%d is_natives=%d", i, lib->name, lib->is_required, lib->is_natives);
        if (!lib->is_required || !lib->is_natives) continue;

        char rel[MC_PATH_MAX];
        mc_library_natives_path(lib->name, lib->natives_key, rel, sizeof(rel));
        if (!rel[0]) { mc_info("download_natives: no natives_path resolved"); continue; }
        char jar_path[MC_PATH_MAX];
        mc_path_join(libraries_dir, rel, jar_path, sizeof(jar_path));
        mc_info("download_natives: checking jar_path=%s, exists=%d", jar_path, mc_path_exists(jar_path));
        if (!mc_path_exists(jar_path)) continue;

        mc_info("Extracting natives from %s", rel);
        mc_zip_extract(jar_path, natives_dir);
        extracted++;
    }

    if (has_base) { mc_version_free(base_buf); free(base_buf); }
    mc_info("Natives extracted to %s (%d jars processed)", natives_dir, extracted);
    return 1;
}

static int download_assets(McVersion *v, const char *mc_dir, DownloadPool &pool) {
    // Use inherited version's asset index if this version has none
    McVersion *asset_v = v;
    McVersion *asset_buf = nullptr;
    if (!v->asset_index.id[0] && v->inherits_from[0]) {
        char base_json_path[MC_PATH_MAX];
        snprintf(base_json_path, sizeof(base_json_path), "%s/versions/%s/%s.json", mc_dir, v->inherits_from, v->inherits_from);
        if (mc_path_exists(base_json_path)) {
            asset_buf = (McVersion *)malloc(sizeof(McVersion));
            if (asset_buf) {
                mc_version_init(asset_buf);
                if (mc_version_parse_file(asset_buf, base_json_path))
                    asset_v = asset_buf;
                else
                    { free(asset_buf); asset_buf = nullptr; }
            }
        }
    }
    McAssetIndex idx;
    if (!mc_asset_index_fetch(&idx, asset_v, mc_dir)) {
        mc_error("Failed to fetch asset index");
        if (asset_buf) { mc_version_free(asset_buf); free(asset_buf); }
        return 0;
    }
    mc_info("Asset index: %d objects", idx.count);

    char objects_dir[MC_PATH_MAX];
    mc_path_join3(mc_dir, "assets", "objects", objects_dir, sizeof(objects_dir));
    mc_path_mkdir_p(objects_dir);

    std::vector<WorkItem> works;
    int total_objects = idx.count;
    for (int i = 0; i < total_objects; i++) {
        char obj_path[MC_PATH_MAX];
        mc_asset_object_path(idx.objects[i].hash, mc_dir, obj_path, sizeof(obj_path));
        if (mc_path_exists(obj_path)) {
            char actual[64];
            if (mc_hash_file_sha1(obj_path, actual, sizeof(actual)) &&
                mc_stricmp(actual, idx.objects[i].hash) == 0)
                continue;
        }
        char asset_url[2048];
        mc_asset_url(idx.objects[i].hash, asset_url, sizeof(asset_url));
        char primary[2048], fallback[2048];
        translate_url(asset_url, primary, sizeof(primary));
        fallback[0] = '\0';
        if (g_mirror && strcmp(g_mirror, "mojang") != 0)
            strncpy(fallback, asset_url, sizeof(fallback) - 1);
        char parent[MC_PATH_MAX];
        mc_path_dirname(obj_path, parent, sizeof(parent));
        mc_path_mkdir_p(parent);
        WorkItem w;
        w.url = primary;
        w.fallback_url = fallback;
        w.output_path = obj_path;
        w.expected_sha1 = idx.objects[i].hash;
        w.expected_size = idx.objects[i].size;
        works.push_back(std::move(w));
    }

    run_works(pool, works, mc_i18n("download_assets"));
    int ok = 0, fail = 0, skip = total_objects - static_cast<int>(works.size());
    for (auto &w : works) { if (w.success) ok++; else fail++; }
    mc_info("Assets: %d ok, %d skipped, %d failed", ok, skip, fail);
    mc_asset_index_free(&idx);
    if (asset_buf) { mc_version_free(asset_buf); free(asset_buf); }
    return fail == 0;
}

static int download_logging(McVersion *v, const char *mc_dir) {
    if (!v->logging_client_url[0]) { mc_info("No logging config"); return 1; }
    char log_path[MC_PATH_MAX];
    mc_path_join3(mc_dir, "assets", "logs", log_path, sizeof(log_path));
    mc_path_mkdir_p(log_path);
    char log_file[MC_PATH_MAX];
    const char *fname = mc_path_filename(v->logging_client_url);
    if (!fname || !*fname) fname = "log4j.xml";
    mc_path_join(log_path, fname, log_file, sizeof(log_file));
    char primary[2048], fallback[2048];
    translate_url(v->logging_client_url, primary, sizeof(primary));
    fallback[0] = '\0';
    if (g_mirror && strcmp(g_mirror, "mojang") != 0)
        strncpy(fallback, v->logging_client_url, sizeof(fallback) - 1);
    if (download_file_with_progress(primary, log_file, nullptr, 0, "logging")) {
        mc_info("Logging config saved: %s", log_file);
        return 1;
    }
    if (fallback[0] && download_file_with_progress(fallback, log_file, nullptr, 0, "logging")) {
        mc_info("Logging config saved (fallback): %s", log_file);
        return 1;
    }
    mc_error("Failed to download logging config");
    return 0;
}

static int cmd_java_download(int major_version, const char *mc_dir, int threads) {
    mc_info("Downloading Java %d runtime to %s...", major_version, mc_dir);

    McJavaFileList files;
    if (!mc_java_download_manifest(major_version, g_mirror, &files)) {
        mc_error("Failed to get Java runtime file list");
        return 1;
    }
    mc_info("Java runtime: %d files to download", files.count);

    char base_dir[MC_PATH_MAX];
    mc_path_join(mc_dir, "java-runtime", base_dir, sizeof(base_dir));
    mc_path_mkdir_p(base_dir);

    std::vector<WorkItem> works;
    for (int i = 0; i < files.count; i++) {
        McJavaFile *f = &files.files[i];
        char full_path[MC_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, f->path);
        char parent[MC_PATH_MAX];
        mc_path_dirname(full_path, parent, sizeof(parent));
        mc_path_mkdir_p(parent);

        if (mc_path_exists(full_path)) {
            char actual[64];
            if (mc_hash_file_sha1(full_path, actual, sizeof(actual)) &&
                mc_stricmp(actual, f->sha1) == 0)
                continue;
        }

        char final_url[2048];
        strncpy(final_url, f->url, sizeof(final_url) - 1);
        char translated[2048];
        if (g_mirror && strcmp(g_mirror, "mojang") != 0 &&
            mc_download_translate_mojang_url(f->url, translated, sizeof(translated), g_mirror))
            strncpy(final_url, translated, sizeof(final_url) - 1);

        WorkItem w;
        w.url = final_url;
        w.output_path = full_path;
        w.expected_sha1 = f->sha1;
        w.expected_size = f->size;
        works.push_back(std::move(w));
    }
    int total = files.count;
    mc_java_file_list_free(&files);

    int skip = total - (int)works.size();
    DownloadPool pool(threads);
    run_works(pool, works, mc_i18n("java_runtime"));
    int ok = 0, fail = 0;
    for (auto &w : works) { if (w.success) ok++; else fail++; }
    mc_info("Java runtime: %d ok, %d skipped, %d failed", ok, skip, fail);
    return fail == 0 ? 0 : 1;
}

static int cmd_mc(const char *version_id, const char *output_dir, int threads) {
    mc_info("Downloading Minecraft %s...", version_id);
    McVersion *v = new_version();
    if (!v) return 1;
    if (!download_version_json(v, version_id, output_dir)) { del_version(v); return 1; }

    download_client_jar(v, output_dir);
    {
        DownloadPool pool(threads);
        download_libraries(v, output_dir, pool);
    }
    {
        DownloadPool pool(threads);
        download_natives(v, output_dir, pool);
    }
    {
        DownloadPool pool(threads);
        download_assets(v, output_dir, pool);
    }
    download_logging(v, output_dir);

    del_version(v);
    mc_info("Download complete for %s", version_id);
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    mc_console_init();
    mc_log_set_level(MC_LOG_INFO);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    if (mc_mirror_load_config("mirrors.json"))
        mc_info("Loaded mirror config from mirrors.json");

    mc_qt_download_init();
    mc_qt_dns_prefetch();

    // Hard 600s timeout to prevent hanging
    QTimer::singleShot(600000, [&app]() {
        mc_error("Download timed out after 600s, exiting");
        app.exit(1);
    });

    const char *lang = nullptr;
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) lang = argv[++i];
    if (lang) mc_i18n_set(lang);

    if (argc < 2) { print_help(); return 0; }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) { print_help(); return 0; }

    const char *output_dir = ".";
    const char *mirror_type = "bmclapi";
    const char *platform_opt = nullptr;
    int thread_count = 16;

    const char *cmd = argv[1];

    if (strcmp(cmd, "java") == 0 && argc >= 3) {
        int java_ver = atoi(argv[2]);
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) output_dir = argv[++i];
            else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) mirror_type = argv[++i];
            else if (strcmp(argv[i], "--platform") == 0 && i + 1 < argc) platform_opt = argv[++i];
            else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
                thread_count = std::max(1, std::min(128, atoi(argv[++i])));
        }
        probe_and_select_mirror(&mirror_type);
        g_mirror = mirror_type;
        if (platform_opt) mc_platform_set(platform_opt);
        return cmd_java_download(java_ver, output_dir, thread_count);
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) output_dir = argv[++i];
        else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) mirror_type = argv[++i];
        else if (strcmp(argv[i], "--platform") == 0 && i + 1 < argc) platform_opt = argv[++i];
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            thread_count = atoi(argv[++i]);
            if (thread_count > 128) thread_count = 128;
        }
    }
    probe_and_select_mirror(&mirror_type);
    g_mirror = mirror_type;
    if (platform_opt) mc_platform_set(platform_opt);

    if (strcmp(cmd, "mc") == 0 && argc >= 3) {
        return cmd_mc(argv[2], output_dir, thread_count);
    }
    if (strcmp(cmd, "client") == 0 && argc >= 3) {
        McVersion *v = new_version(); if (!v) return 1;
        if (!download_version_json(v, argv[2], output_dir)) { del_version(v); return 1; }
        int r = download_client_jar(v, output_dir) ? 0 : 1;
        del_version(v); return r;
    }
    if (strcmp(cmd, "server") == 0 && argc >= 3) {
        McVersion *v = new_version(); if (!v) return 1;
        if (!fetch_version_or_local(v, argv[2], output_dir)) { del_version(v); return 1; }
        int r = download_server_jar(v, output_dir) ? 0 : 1;
        del_version(v); return r;
    }
    if (strcmp(cmd, "libraries") == 0 && argc >= 3) {
        McVersion *v = new_version(); if (!v) return 1;
        if (!fetch_version_or_local(v, argv[2], output_dir)) { del_version(v); return 1; }
        DownloadPool pool(thread_count);
        int r = download_libraries(v, output_dir, pool) ? 0 : 1;
        del_version(v); return r;
    }
    if (strcmp(cmd, "natives") == 0 && argc >= 3) {
        McVersion *v = new_version(); if (!v) return 1;
        if (!fetch_version_or_local(v, argv[2], output_dir)) { del_version(v); return 1; }
        DownloadPool pool(thread_count);
        int r = download_natives(v, output_dir, pool) ? 0 : 1;
        del_version(v); return r;
    }
    if (strcmp(cmd, "assets") == 0 && argc >= 3) {
        McVersion *v = new_version(); if (!v) return 1;
        if (!fetch_version_or_local(v, argv[2], output_dir)) { del_version(v); return 1; }
        DownloadPool pool(thread_count);
        int r = download_assets(v, output_dir, pool) ? 0 : 1;
        del_version(v); return r;
    }
    if (strcmp(cmd, "logging") == 0 && argc >= 3) {
        McVersion *v = new_version(); if (!v) return 1;
        if (!fetch_version_or_local(v, argv[2], output_dir)) { del_version(v); return 1; }
        int r = download_logging(v, output_dir) ? 0 : 1;
        del_version(v); return r;
    }
    if (strcmp(cmd, "mod") == 0 && argc >= 3) {
        const char *url = argv[2];
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) output_dir = argv[++i];
            else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
                thread_count = atoi(argv[++i]);
                if (thread_count > 128) thread_count = 128;
            }
        }
        // Construct output path: <dir>/<filename-from-url>
        char out_path[1024];
        const char *fname = mc_path_filename(url);
        snprintf(out_path, sizeof(out_path), "%s/%s", output_dir, fname);
        if (download_file_with_progress(url, out_path, nullptr, 0, "Mod download")) {
            mc_info("Downloaded mod: %s", out_path); return 0;
        }
        mc_error("Mod download failed"); return 1;
    }

    if (strcmp(cmd, "url") == 0 && argc >= 3) {
        const char *url = argv[2];
        const char *out_path = nullptr;
        for (int i = 3; i < argc; i++)
            if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) out_path = argv[++i];
        if (!out_path) out_path = mc_path_filename(url);
        if (download_file_with_progress(url, out_path, nullptr, 0, "URL download")) {
            mc_info("Downloaded: %s", out_path); return 0;
        }
        mc_error("Download failed"); return 1;
    }

    mc_console_printf("%s: %s\n\n", mc_i18n("unknown_command"), cmd);
    print_help();
    mc_qt_download_cleanup();
    return 1;
}
