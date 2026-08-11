/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_version.h"
#include "mc_http.h"
#include "mc_library.h"
#include "mc_str.h"
#include "mc_manifest.h"
#include "mc_download.h"
#include "mc_path.h"
#include "mc_log.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iostream>

void mc_version_init(McVersion *v) {
    if (!v) return;
    void *base = v;
    memset(base, 0, sizeof(McVersion) - sizeof(QJsonObject));
    new (&v->raw_json) QJsonObject();
}

static void parse_library(McVersion *v, const QJsonObject &lib_json) {
    if (v->library_count >= MC_MAX_LIBRARIES) return;
    McLibrary *lib = &v->libraries[v->library_count];
    memset(lib, 0, sizeof(McLibrary));

    QString name = lib_json.value("name").toString();
    strncpy(lib->name, name.toUtf8().constData(), sizeof(lib->name) - 1);
    lib->is_required = 1;

    QJsonValue rules = lib_json.value("rules");
    if (!rules.isUndefined()) {
        lib->is_required = mc_version_evaluate_rules(rules);
        mc_debug("  parse_library '%s': rules -> is_required=%d", lib->name, lib->is_required);
    }

    QJsonObject downloads = lib_json.value("downloads").toObject();
    if (!downloads.isEmpty()) {
        QJsonObject artifact = downloads.value("artifact").toObject();
        if (!artifact.isEmpty()) {
            strncpy(lib->url, artifact.value("url").toString().toUtf8().constData(), sizeof(lib->url) - 1);
            strncpy(lib->path, artifact.value("path").toString().toUtf8().constData(), sizeof(lib->path) - 1);
            strncpy(lib->sha1, artifact.value("sha1").toString().toUtf8().constData(), sizeof(lib->sha1) - 1);
            lib->size = (long)artifact.value("size").toDouble(0);
        }

        QJsonObject classifiers = downloads.value("classifiers").toObject();
        if (!classifiers.isEmpty()) {
            // Pick the native-classifier for THIS platform (natives-windows /
            // natives-linux / natives-osx), preferring the arch-suffixed key.
            char prefix[32];
            snprintf(prefix, sizeof(prefix), "natives-%s", mc_platform_get());
            const char *arch = mc_platform_arch_get();
            const char *suffix = (strcmp(arch, "x86") == 0) ? "-32" : "-64";
            char keyExplicit[64];
            snprintf(keyExplicit, sizeof(keyExplicit), "%s%s", prefix, suffix);

            auto tryClass = [lib, &classifiers](const QString &key) -> bool {
                QJsonObject o = classifiers.value(key).toObject();
                if (o.isEmpty()) return false;
                lib->is_natives = 1;
                strncpy(lib->natives_key, key.toUtf8().constData(), sizeof(lib->natives_key) - 1);
                strncpy(lib->classifier_url, o.value("url").toString().toUtf8().constData(), sizeof(lib->classifier_url) - 1);
                strncpy(lib->classifier_sha1, o.value("sha1").toString().toUtf8().constData(), sizeof(lib->classifier_sha1) - 1);
                lib->classifier_size = (long)o.value("size").toDouble(0);
                return true;
            };

            if (!tryClass(QString::fromUtf8(keyExplicit)))
                tryClass(QString::fromUtf8(prefix));
        }
    } else {
        lib->is_required = 1;
        QString url = lib_json.value("url").toString();
        // Construct full URL: base URL + library path (e.g., maven.fabricmc.net/net/.../fabric-loader-0.19.3.jar)
        char libPath[512];
        mc_library_resolve_path(name.toUtf8().constData(), libPath, sizeof(libPath));
        QByteArray urlBytes = url.toUtf8();
        const char *urlStr = urlBytes.constData();
        if (urlStr && *urlStr && libPath[0]) {
            size_t len = strlen(urlStr);
            while (len > 0 && urlStr[len - 1] == '/') len--;
            size_t plen = strlen(libPath);
            if (len + 1 + plen < sizeof(lib->url)) {
                memcpy(lib->url, urlStr, len);
                lib->url[len] = '/';
                memcpy(lib->url + len + 1, libPath, plen);
                lib->url[len + 1 + plen] = '\0';
            }
        }
    }

    QJsonObject natives = lib_json.value("natives").toObject();
    if (!natives.isEmpty() && !lib->is_natives) {
        QString platform = QString::fromUtf8(mc_platform_get());
        QString nw = natives.value(platform).toString();
        if (!nw.isEmpty()) {
            lib->is_natives = 1;
            strncpy(lib->natives_key, nw.toUtf8().constData(), sizeof(lib->natives_key) - 1);
        }
    }

    if (!lib->is_natives) {
        const char *last = strrchr(lib->name, ':');
        if (last && strncmp(last + 1, "natives-", 8) == 0) {
            lib->is_natives = 1;
            strncpy(lib->natives_key, last + 1, sizeof(lib->natives_key) - 1);
        }
    }

    QJsonObject extract = lib_json.value("extract").toObject();
    if (!extract.isEmpty()) {
        QJsonArray exclude = extract.value("exclude").toArray();
        if (!exclude.isEmpty()) {
            for (int i = 0; i < exclude.size() && lib->exclude_count < 8; i++) {
                QString item = exclude[i].toString();
                if (!item.isEmpty())
                    strncpy(lib->extract_exclude[lib->exclude_count++], item.toUtf8().constData(), 63);
            }
        }
    }

    v->library_count++;
}

static int rule_matches_current_os(const QJsonValue &os) {
    if (os.isUndefined()) return 1;
    const char *current_os = mc_platform_get();
    const char *current_arch = mc_platform_arch_get();

    if (os.isString()) {
        QString s = os.toString();
        QByteArray sBytes = s.toUtf8();
        const char *cstr = sBytes.constData();
        if (cstr[0] == '@' && cstr[1] == '{') {
            const char *content = cstr + 2;
            const char *end = content + strlen(content) - 1;
            if (*end == '}') {
                char cond[256];
                size_t len = (size_t)(end - content);
                if (len >= sizeof(cond)) len = sizeof(cond) - 1;
                memcpy(cond, content, len);
                cond[len] = '\0';
                char *eq = strchr(cond, '=');
                if (eq) {
                    *eq = '\0';
                    const char *key = cond;
                    const char *val = eq + 1;
                    if (strcmp(key, "name") == 0)
                        return (strcmp(val, current_os) == 0) ? 1 : 0;
                    if (strcmp(key, "arch") == 0) {
                        if (strcmp(val, "x86") == 0) return (strcmp(current_arch, "x86") == 0) ? 1 : 0;
                        if (strcmp(val, "x64") == 0) return (strcmp(current_arch, "x64") == 0) ? 1 : 0;
                    }
                }
            }
        }
        return 1;
    }

    if (os.isObject()) {
        QJsonObject osObj = os.toObject();
        QString name = osObj.value("name").toString();
        if (!name.isEmpty()) {
            if (name.compare(QLatin1String(current_os), Qt::CaseInsensitive) != 0) return 0;
        }
        QString arch = osObj.value("arch").toString();
        if (!arch.isEmpty()) {
            if (arch == "x86" && strcmp(current_arch, "x86") != 0) return 0;
            if (arch == "x64" && strcmp(current_arch, "x64") != 0) return 0;
        }
        return 1;
    }

    return 1;
}

static int feature_matches(const QJsonObject &features) {
    if (features.isEmpty()) return 1;
    for (auto it = features.begin(); it != features.end(); ++it) {
        if (it.key() == "is_demo_user") {
            if (it.value().isBool() && it.value().toBool()) return 0;
            if (it.value().isDouble() && it.value().toDouble() != 0) return 0;
        }
        if (it.key() == "has_custom_resolution") {
            if (it.value().isBool() && it.value().toBool()) return 0;
            if (it.value().isDouble() && it.value().toDouble() != 0) return 0;
        }
        if (it.key() == "has_quick_plays_support") {
            if (it.value().isBool() && it.value().toBool()) return 0;
            if (it.value().isDouble() && it.value().toDouble() != 0) return 0;
        }
        if (it.key().startsWith("is_quick_play")) {
            if (it.value().isBool() && it.value().toBool()) return 0;
            if (it.value().isDouble() && it.value().toDouble() != 0) return 0;
        }
    }
    return 0;
}

int mc_version_evaluate_rules(const QJsonValue &rules_node) {
    if (rules_node.isUndefined()) return 1;

    if (rules_node.isObject()) {
        QJsonObject rule = rules_node.toObject();
        QString action = rule.value("action").toString();
        QJsonValue os = rule.value("os");
        QJsonValue features = rule.value("features");
        if (!os.isUndefined()) {
            if (rule_matches_current_os(os))
                return (action == "allow") ? 1 : 0;
            return 0;
        }
        if (!features.isUndefined()) {
            if (feature_matches(features.toObject()))
                return (action == "allow") ? 1 : 0;
            return 0;
        }
        return (action == "allow") ? 1 : 0;
    }

    if (!rules_node.isArray()) return 0;
    QJsonArray arr = rules_node.toArray();
    int result = 0;
    for (int i = 0; i < arr.size(); i++) {
        QJsonValue rule_val = arr[i];
        if (!rule_val.isObject()) continue;
        QJsonObject rule = rule_val.toObject();
        QString action = rule.value("action").toString();
        QJsonValue os = rule.value("os");
        QJsonValue features = rule.value("features");
        if (!os.isUndefined()) {
            if (rule_matches_current_os(os))
                result = (action == "allow") ? 1 : 0;
        } else if (!features.isUndefined()) {
            if (feature_matches(features.toObject()))
                result = (action == "allow") ? 1 : 0;
        } else {
            result = (action == "allow") ? 1 : 0;
        }
    }
    return result;
}

int mc_version_parse(McVersion *v, const char *json_data) {
    if (!v || !json_data) return 0;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_data), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return 0;
    QJsonObject j = doc.object();
    v->raw_json = j;

    QString s;
    s = j.value("id").toString(); strncpy(v->id, s.toUtf8().constData(), sizeof(v->id) - 1);
    s = j.value("type").toString(); strncpy(v->type, s.toUtf8().constData(), sizeof(v->type) - 1);
    s = j.value("mainClass").toString(); strncpy(v->main_class, s.toUtf8().constData(), sizeof(v->main_class) - 1);
    s = j.value("minecraftArguments").toString(); strncpy(v->minecraft_arguments, s.toUtf8().constData(), sizeof(v->minecraft_arguments) - 1);
    s = j.value("inheritsFrom").toString(); strncpy(v->inherits_from, s.toUtf8().constData(), sizeof(v->inherits_from) - 1);
    s = j.value("jar").toString(); strncpy(v->jar, s.toUtf8().constData(), sizeof(v->jar) - 1);
    s = j.value("assets").toString(); strncpy(v->assets, s.toUtf8().constData(), sizeof(v->assets) - 1);

    QJsonObject ai = j.value("assetIndex").toObject();
    if (!ai.isEmpty()) {
        strncpy(v->asset_index.id, ai.value("id").toString().toUtf8().constData(), sizeof(v->asset_index.id) - 1);
        strncpy(v->asset_index.url, ai.value("url").toString().toUtf8().constData(), sizeof(v->asset_index.url) - 1);
        strncpy(v->asset_index.sha1, ai.value("sha1").toString().toUtf8().constData(), sizeof(v->asset_index.sha1) - 1);
        v->asset_index.size = (long)ai.value("size").toDouble(0);
        v->asset_index.total_size = (long)ai.value("totalSize").toDouble(0);
    }

    QJsonObject jv = j.value("javaVersion").toObject();
    if (!jv.isEmpty()) {
        v->java_major_version = (int)jv.value("majorVersion").toDouble(0);
        strncpy(v->java_component, jv.value("component").toString().toUtf8().constData(), sizeof(v->java_component) - 1);
    }

    QJsonObject dl = j.value("downloads").toObject();
    if (!dl.isEmpty()) {
        QJsonObject client = dl.value("client").toObject();
        if (!client.isEmpty()) {
            strncpy(v->client_url, client.value("url").toString().toUtf8().constData(), sizeof(v->client_url) - 1);
            strncpy(v->client_sha1, client.value("sha1").toString().toUtf8().constData(), sizeof(v->client_sha1) - 1);
            v->client_size = (long)client.value("size").toDouble(0);
        }
        QJsonObject server = dl.value("server").toObject();
        if (!server.isEmpty()) {
            strncpy(v->server_url, server.value("url").toString().toUtf8().constData(), sizeof(v->server_url) - 1);
            strncpy(v->server_sha1, server.value("sha1").toString().toUtf8().constData(), sizeof(v->server_sha1) - 1);
            v->server_size = (long)server.value("size").toDouble(0);
        }
    }

    QJsonObject logcfg = j.value("logging").toObject();
    if (!logcfg.isEmpty()) {
        QJsonObject lclient = logcfg.value("client").toObject();
        if (!lclient.isEmpty()) {
            QJsonObject file = lclient.value("file").toObject();
            if (!file.isEmpty()) {
                strncpy(v->logging_client_url, file.value("url").toString().toUtf8().constData(), sizeof(v->logging_client_url) - 1);
                strncpy(v->logging_client_sha1, file.value("sha1").toString().toUtf8().constData(), sizeof(v->logging_client_sha1) - 1);
            }
        }
    }

    QJsonArray libs = j.value("libraries").toArray();
    if (!libs.isEmpty()) {
        for (int i = 0; i < libs.size(); i++) {
            QJsonValue lib_val = libs[i];
            if (lib_val.isObject())
                parse_library(v, lib_val.toObject());
        }
    }

    v->is_loaded = 1;
    return 1;
}

int mc_version_parse_file(McVersion *v, const char *path) {
    if (!v || !path) return 0;
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
    int ok = mc_version_parse(v, data);
    free(data);
    return ok;
}

int mc_version_fetch(McVersion *v, const char *url) {
    if (!v || !url) return 0;
    strncpy(v->source_url, url, sizeof(v->source_url) - 1);
    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    mc_info("Fetching version JSON: %s", url);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch version JSON from %s", url);
        if (resp) mc_http_response_free(resp);
        return 0;
    }
    int ok = mc_version_parse(v, resp->data);
    mc_http_response_free(resp);
    return ok;
}

int mc_version_fetch_by_id(McVersion *v, const char *version_id) {
    return mc_version_fetch_by_id_mirror(v, version_id, NULL);
}

int mc_version_fetch_by_id_mirror(McVersion *v, const char *version_id, const char *mirror_type) {
    if (!v || !version_id) return 0;
    McManifest *manifest = (McManifest *)malloc(sizeof(McManifest));
    if (!manifest) return 0;
    memset(manifest, 0, sizeof(McManifest));
    if (!mc_manifest_fetch_mirror(manifest, 0, mirror_type)) { free(manifest); return 0; }
    McVersionEntry entry;
    if (!mc_manifest_find(manifest, version_id, &entry)) {
        free(manifest);
        mc_error("Version '%s' not found in manifest", version_id);
        return 0;
    }
    free(manifest);
    if (mirror_type && strcmp(mirror_type, "mojang") != 0) {
        char translated[512];
        if (mc_download_translate_mojang_url(entry.url, translated, sizeof(translated), mirror_type)) {
            strncpy(entry.url, translated, sizeof(entry.url) - 1);
        }
    }
    return mc_version_fetch(v, entry.url);
}

void mc_version_free(McVersion *v) {
    if (!v) return;
    v->raw_json = QJsonObject();
    v->is_loaded = 0;
}

static char g_platform[16] = "";
static char g_arch[16] = "";

const char *mc_platform_get(void) {
    if (!g_platform[0]) {
#ifdef _WIN32
        strcpy(g_platform, "windows");
#elif defined(__APPLE__)
        strcpy(g_platform, "osx");
#else
        strcpy(g_platform, "linux");
#endif
    }
    return g_platform;
}

void mc_platform_set(const char *platform) {
    if (platform)
        strncpy(g_platform, platform, sizeof(g_platform) - 1);
}

const char *mc_platform_arch_get(void) {
    if (!g_arch[0]) {
#ifdef _WIN64
        strcpy(g_arch, "x64");
#elif defined(_WIN32)
        strcpy(g_arch, "x86");
#elif defined(__x86_64__) || defined(__amd64__)
        strcpy(g_arch, "x64");
#elif defined(__aarch64__) || defined(__arm64__)
        strcpy(g_arch, "arm64");
#elif defined(__arm__)
        strcpy(g_arch, "arm32");
#else
        strcpy(g_arch, "x64");
#endif
    }
    return g_arch;
}

void mc_platform_arch_set(const char *arch) {
    if (arch)
        strncpy(g_arch, arch, sizeof(g_arch) - 1);
}
