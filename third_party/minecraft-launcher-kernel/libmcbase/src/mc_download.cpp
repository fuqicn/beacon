/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_download.h"
#include "mc_http.h"
#include "mc_hash.h"
#include "mc_path.h"
#include "mc_str.h"
#include "mc_log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <chrono>
#include <mutex>

#include <QtCore/QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#define MIRROR_TEST_URL "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"

// Global download-source state ("auto" = probe best mirror at call time).
static std::mutex g_download_mirror_mutex;
static char g_download_mirror_type[64] = "auto";
static char g_download_mirror_effective[64] = "";
static std::chrono::steady_clock::time_point g_download_mirror_effective_at;
static thread_local char t_download_effective[64] = "";

void mc_download_set_mirror(const char *mirror_type) {
    std::lock_guard<std::mutex> lk(g_download_mirror_mutex);
    if (!mirror_type || !mirror_type[0]) {
        strncpy(g_download_mirror_type, "auto", sizeof(g_download_mirror_type) - 1);
    } else {
        strncpy(g_download_mirror_type, mirror_type, sizeof(g_download_mirror_type) - 1);
    }
    g_download_mirror_type[sizeof(g_download_mirror_type) - 1] = '\0';
    g_download_mirror_effective[0] = '\0';
    mc_info("Download source set to: %s", g_download_mirror_type);
}

const char *mc_download_mirror(void) {
    std::lock_guard<std::mutex> lk(g_download_mirror_mutex);
    return g_download_mirror_type;
}

const char *mc_download_effective_mirror(void) {
    char resolved[64];
    {
        std::lock_guard<std::mutex> lk(g_download_mirror_mutex);
        if (g_download_mirror_type[0] && strcmp(g_download_mirror_type, "auto") != 0) {
            strncpy(t_download_effective, g_download_mirror_type, sizeof(t_download_effective) - 1);
            return t_download_effective;
        }
        auto now = std::chrono::steady_clock::now();
        if (g_download_mirror_effective[0] &&
            now - g_download_mirror_effective_at < std::chrono::minutes(10)) {
            strncpy(t_download_effective, g_download_mirror_effective, sizeof(t_download_effective) - 1);
            return t_download_effective;
        }
        strncpy(resolved, "mojang", sizeof(resolved) - 1);
    }

    McMirrorProbe results[MC_MAX_MIRROR_TYPES];
    int count = mc_mirror_probe_all(results, MC_MAX_MIRROR_TYPES);
    const char *best = mc_mirror_select_best(results, count);
    if (!best) best = "mojang";
    strncpy(resolved, best, sizeof(resolved) - 1);

    {
        std::lock_guard<std::mutex> lk(g_download_mirror_mutex);
        strncpy(g_download_mirror_effective, resolved, sizeof(g_download_mirror_effective) - 1);
        g_download_mirror_effective_at = std::chrono::steady_clock::now();
        strncpy(t_download_effective, resolved, sizeof(t_download_effective) - 1);
    }
    return t_download_effective;
}

int mc_download_translate_mojang_url_auto(const char *url, char *mirror, size_t mirror_size) {
    return mc_download_translate_mojang_url(url, mirror, mirror_size, mc_download_effective_mirror());
}

// Loaded mirror configs (replaces hardcoded logic when populated)
static McMirrorEntry g_mirror_configs[MC_MIRROR_MAX_ENTRIES];
static int g_mirror_config_count = 0;

static const McMirrorEntry *find_mirror_entry(const char *mirror_type) {
    if (!mirror_type) return NULL;
    for (int i = 0; i < g_mirror_config_count; i++)
        if (strcmp(g_mirror_configs[i].name, mirror_type) == 0)
            return &g_mirror_configs[i];
    return NULL;
}

// Apply full-URL domain substitution: replace src_domain with dest_domain in url.
static int substitute_domain(const char *url, const char *src_domain, const char *dest_domain,
                             char *out, size_t out_size) {
    const char *found = strstr(url, src_domain);
    if (!found) return 0;
    // Make sure it's a hostname boundary (preceded by :// or start of string)
    const char *before = found - 1;
    if (before >= url && before[0] != ':' && *(before - 1) != '/' && found != url) {
        // Not a hostname boundary, skip
        // Search for next occurrence
        const char *next = strstr(found + 1, src_domain);
        if (!next) return 0;
        found = next;
        before = found - 1;
        if (!(before >= url && (before[0] == ':' && *(before-1) == '/'))) {
            return 0;
        }
    }
    // Build the new URL: everything before src_domain + dest_domain + everything after
    size_t prefix_len = (size_t)(found - url);
    size_t src_len = strlen(src_domain);
    size_t suffix_len = strlen(found + src_len);
    size_t dest_len = strlen(dest_domain);
    if (prefix_len + dest_len + suffix_len + 1 > out_size) return 0;
    memcpy(out, url, prefix_len);
    memcpy(out + prefix_len, dest_domain, dest_len);
    memcpy(out + prefix_len + dest_len, found + src_len, suffix_len + 1);
    return 1;
}

// Full-URL rewrite: if url contains cdn_domain, replace that domain with base_url.
static int apply_cdn_rewrite(const McMirrorEntry *entry, const char *url, char *out, size_t out_size) {
    if (!entry->cdn_domain[0]) return 0;
    return substitute_domain(url, entry->cdn_domain, entry->base_url, out, out_size);
}

static int apply_mirror_entry(const McMirrorEntry *entry, const char *url, char *mirror, size_t mirror_size) {
    // 1. Try CDN domain substitution first (full URL rewrite)
    if (apply_cdn_rewrite(entry, url, mirror, mirror_size)) return 1;

    // 2. Try rules in order
    for (int i = 0; i < entry->rule_count; i++) {
        const char *match = entry->rules[i].match;
        const char *found = strstr(url, match);
        if (!found) continue;
        // Verify it's in the hostname part (before the first '/')
        const char *path_start = strchr(found, '/');
        if (path_start && path_start - found < (int)strlen(match)) {
            // Match extends into the path; skip
            continue;
        }
        const char *after_match = found + strlen(match);
        if (entry->rules[i].prefix[0]) {
            snprintf(mirror, mirror_size, "%s%s%s", entry->base_url, entry->rules[i].prefix, after_match);
        } else {
            snprintf(mirror, mirror_size, "%s%s", entry->base_url, after_match);
        }
        return 1;
    }

    // 3. No rule matched: replace hostname with base_url
    const char *scheme_end = strstr(url, "://");
    if (!scheme_end) return 0;
    const char *path_start = strchr(scheme_end + 3, '/');
    if (!path_start) {
        snprintf(mirror, mirror_size, "%s", entry->base_url);
        return 1;
    }
    snprintf(mirror, mirror_size, "%s%s", entry->base_url, path_start);
    return 1;
}

int mc_mirror_load_config_json(const char *json_data) {
    if (!json_data) return 0;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_data), &err);
    if (err.error != QJsonParseError::NoError) return 0;
    if (!doc.isArray()) return 0;
    QJsonArray arr = doc.array();
    for (int ai = 0; ai < arr.size() && g_mirror_config_count < MC_MIRROR_MAX_ENTRIES; ai++) {
        QJsonObject obj = arr[ai].toObject();
        if (obj.isEmpty()) continue;
        McMirrorEntry *entry = &g_mirror_configs[g_mirror_config_count];
        memset(entry, 0, sizeof(*entry));
        QString name = obj.value("name").toString();
        QString base = obj.value("base_url").toString();
        if (name.isEmpty() || base.isEmpty()) continue;
        strncpy(entry->name, name.toUtf8().constData(), sizeof(entry->name) - 1);
        strncpy(entry->base_url, base.toUtf8().constData(), sizeof(entry->base_url) - 1);
        QString cdn = obj.value("cdn_domain").toString();
        if (!cdn.isEmpty())
            strncpy(entry->cdn_domain, cdn.toUtf8().constData(), sizeof(entry->cdn_domain) - 1);
        QString java_alt = obj.value("java_alternative").toString();
        if (!java_alt.isEmpty())
            strncpy(entry->java_alternative, java_alt.toUtf8().constData(), sizeof(entry->java_alternative) - 1);
        QJsonArray rules = obj.value("rules").toArray();
        for (int ri = 0; ri < rules.size() && entry->rule_count < MC_MIRROR_MAX_RULES; ri++) {
            QJsonObject r = rules[ri].toObject();
            QString match = r.value("match").toString();
            QString prefix = r.value("prefix").toString();
            if (match.isEmpty()) continue;
            McMirrorRule *rule = &entry->rules[entry->rule_count];
            strncpy(rule->match, match.toUtf8().constData(), sizeof(rule->match) - 1);
            if (!prefix.isEmpty())
                strncpy(rule->prefix, prefix.toUtf8().constData(), sizeof(rule->prefix) - 1);
            entry->rule_count++;
        }
        g_mirror_config_count++;
    }
    return g_mirror_config_count > 0;
}

int mc_mirror_load_config(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return 0; }
    char *data = (char *)malloc((size_t)len + 1);
    if (!data) { fclose(f); return 0; }
    fread(data, 1, (size_t)len, f);
    data[len] = '\0';
    fclose(f);
    int ok = mc_mirror_load_config_json(data);
    free(data);
    return ok;
}

void mc_mirror_clear_config(void) {
    g_mirror_config_count = 0;
    memset(g_mirror_configs, 0, sizeof(g_mirror_configs));
}

int mc_mirror_config_count(void) { return g_mirror_config_count; }

const McMirrorEntry *mc_mirror_config_get(int index) {
    if (index < 0 || index >= g_mirror_config_count) return NULL;
    return &g_mirror_configs[index];
}

// Translate any URL using the named mirror (not just Mojang URLs).
// Returns 1 on success, 0 if no translation applied (URL passed through unchanged).
int mc_download_translate_url(const char *url, char *out, size_t out_size, const char *mirror_type) {
    if (!url || !out || out_size == 0) return 0;
    if (!mirror_type || strcmp(mirror_type, "mojang") == 0 || strcmp(mirror_type, "auto") == 0) {
        strncpy(out, url, out_size - 1);
        out[out_size - 1] = '\0';
        return 1;
    }
    const McMirrorEntry *entry = find_mirror_entry(mirror_type);
    if (entry)
        return apply_mirror_entry(entry, url, out, out_size);
    // Fallback to mojang-specific translation
    return mc_download_translate_mojang_url(url, out, out_size, mirror_type);
}

int mc_download_translate_mojang_url(const char *url, char *mirror, size_t mirror_size, const char *mirror_type) {
    if (!url || !mirror || mirror_size == 0) return 0;
    // Default mirror type
    if (!mirror_type || strcmp(mirror_type, "mojang") == 0 || strcmp(mirror_type, "auto") == 0) {
        strncpy(mirror, url, mirror_size - 1);
        return 1;
    }
    // Check loaded configs first
    const McMirrorEntry *entry = find_mirror_entry(mirror_type);
    if (entry)
        return apply_mirror_entry(entry, url, mirror, mirror_size);
    // Fall back to custom URL passthrough
    if (strncmp(mirror_type, "http", 4) == 0) {
        strncpy(mirror, mirror_type, mirror_size - 1);
        return 1;
    }

    // Hardcoded fallback for known types
    const char *from_domain = NULL;
    const char *to_domain = NULL;
    int replace_all = 0;

    if (strcmp(mirror_type, "bmclapi") == 0) {
        if (strstr(url, "libraries.minecraft.net")) {
            from_domain = "libraries.minecraft.net";
            to_domain = "bmclapi2.bangbang93.com/maven";
        } else if (strstr(url, "resources.download.minecraft.net")) {
            from_domain = "resources.download.minecraft.net";
            to_domain = "bmclapi2.bangbang93.com/assets";
        } else if (strstr(url, "piston-data.mojang.com") || strstr(url, "piston-meta.mojang.com") ||
                   strstr(url, "launcher.mojang.com") || strstr(url, "launchermeta.mojang.com")) {
            replace_all = 1;
            from_domain = ".mojang.com";
            to_domain = "bmclapi2.bangbang93.com";
        } else if (strstr(url, "maven.fabricmc.net")) {
            from_domain = "maven.fabricmc.net";
            to_domain = "bmclapi2.bangbang93.com/maven";
        } else if (strstr(url, "maven.minecraftforge.net")) {
            from_domain = "maven.minecraftforge.net";
            to_domain = "bmclapi2.bangbang93.com/maven";
        } else if (strstr(url, "maven.neoforged.net")) {
            from_domain = "maven.neoforged.net";
            to_domain = "bmclapi2.bangbang93.com/maven";
        } else {
            return 0;
        }
    } else if (strcmp(mirror_type, "mcbbs") == 0) {
        if (strstr(url, "libraries.minecraft.net") || strstr(url, "maven.fabricmc.net") ||
            strstr(url, "maven.minecraftforge.net") || strstr(url, "maven.neoforged.net")) {
            from_domain = ".net";
            to_domain = "download.mcbbs.net/maven";
            const char *extracted = strstr(url, "://");
            if (extracted) extracted += 3; else extracted = url;
            const char *slash = strchr(extracted, '/');
            if (slash) {
                char path_only[1024];
                strncpy(path_only, slash, sizeof(path_only) - 1);
                char full_url[1024];
                {
                    std::ostringstream oss;
                    oss << "https://download.mcbbs.net/maven" << path_only;
                    strncpy(full_url, oss.str().c_str(), sizeof(full_url) - 1);
                    full_url[sizeof(full_url) - 1] = '\0';
                }
                strncpy(mirror, full_url, mirror_size - 1);
                return 1;
            }
            return 0;
        } else if (strstr(url, "resources.download.minecraft.net")) {
            const char *extracted = strstr(url, "://");
            if (extracted) extracted += 3; else extracted = url;
            const char *slash = strchr(extracted, '/');
            if (slash) {
                char path_only[1024];
                strncpy(path_only, slash, sizeof(path_only) - 1);
                char full_url[1024];
                {
                    std::ostringstream oss;
                    oss << "https://download.mcbbs.net/assets" << path_only;
                    strncpy(full_url, oss.str().c_str(), sizeof(full_url) - 1);
                    full_url[sizeof(full_url) - 1] = '\0';
                }
                strncpy(mirror, full_url, mirror_size - 1);
                return 1;
            }
            return 0;
        } else {
            replace_all = 1;
            from_domain = ".mojang.com";
            to_domain = "download.mcbbs.net";
        }
    } else {
        return 0;
    }

    if (replace_all) {
        // Replace each known Mojang subdomain with the mirror domain
        struct { const char *from; const char *to; } subs[] = {
            { "piston-data.mojang.com", to_domain },
            { "piston-meta.mojang.com", to_domain },
            { "launcher.mojang.com", to_domain },
            { "launchermeta.mojang.com", to_domain },
            { NULL, NULL }
        };
        char *cur = mc_strdup(url);
        if (!cur) return 0;
        for (int i = 0; subs[i].from; i++) {
            if (strstr(cur, subs[i].from)) {
                char *tmp = mc_strreplace(cur, subs[i].from, to_domain);
                free(cur);
                cur = tmp;
                break;
            }
        }
        if (!cur) return 0;
        strncpy(mirror, cur, mirror_size - 1);
        free(cur);
        return 1;
    }

    char *replaced = mc_strreplace(url, from_domain, to_domain);
    if (!replaced) return 0;
    strncpy(mirror, replaced, mirror_size - 1);
    free(replaced);
    return 1;
}

const char *mc_mirror_get_test_url(void) {
    return MIRROR_TEST_URL;
}

int mc_mirror_probe(const char *mirror_type, McMirrorProbe *result) {
    if (!result) return 0;
    memset(result, 0, sizeof(*result));
    if (mirror_type)
        strncpy(result->mirror_type, mirror_type, sizeof(result->mirror_type) - 1);
    else
        strncpy(result->mirror_type, "mojang", sizeof(result->mirror_type) - 1);

    char test_url[2048];
    if (!mc_download_translate_mojang_url(MIRROR_TEST_URL, test_url, sizeof(test_url),
                                          result->mirror_type))
        return 0;

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, MC_MIRROR_PROBE_TIMEOUT_MS);

    auto t1 = std::chrono::steady_clock::now();
    McHttpResponse *resp = mc_http_head(&client, test_url);
    auto t2 = std::chrono::steady_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    result->latency_ms = (double)ms;

    if (resp) {
        result->status_code = (int)resp->status_code;
        result->available = (resp->success && resp->status_code >= 200 && resp->status_code < 400) ? 1 : 0;
        mc_http_response_free(resp);
    }
    return 1;
}

int mc_mirror_probe_all(McMirrorProbe *results, int max_results) {
    if (!results) return 0;
    const char *known_types[] = { "mojang", "bmclapi", "mcimirror", "mcbbs", NULL };
    int count = 0;
    for (int i = 0; known_types[i] && count < max_results; i++)
        if (mc_mirror_probe(known_types[i], &results[count]))
            count++;
    return count;
}

const char *mc_mirror_select_best(McMirrorProbe *results, int count) {
    if (!results || count <= 0) return "mojang";
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (!results[i].available) continue;
        if (best < 0 || results[i].latency_ms < results[best].latency_ms)
            best = i;
    }
    if (best >= 0) return results[best].mirror_type;
    // No mirror available, fall back to mojang
    for (int i = 0; i < count; i++) {
        if (strcmp(results[i].mirror_type, "mojang") == 0)
            return "mojang";
    }
    return "mojang";
}
