/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_search.h"
#include "mc_mod.h"
#include "mc_http.h"
#include "mc_log.h"
#include "mc_download.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QByteArray>
#include <QUrl>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ---- helpers ----

static char *strdup_qstring(const QString &s) {
    if (s.isEmpty()) return nullptr;
    QByteArray ba = s.toUtf8();
    char *r = (char *)malloc(ba.size() + 1);
    if (r) {
        memcpy(r, ba.constData(), ba.size());
        r[ba.size()] = '\0';
    }
    return r;
}

static int safe_int(const QJsonObject &obj, const char *key) {
    return obj.value(key).toInt(0);
}

static long long safe_int64(const QJsonObject &obj, const char *key) {
    return (long long)obj.value(key).toDouble(0);
}

static QString safe_string(const QJsonObject &obj, const char *key) {
    return obj.value(key).toString();
}

static QStringList safe_string_array(const QJsonObject &obj, const char *key) {
    QStringList r;
    QJsonArray a = obj.value(key).toArray();
    for (auto v : a) r.append(v.toString());
    return r;
}

static void apply_mod_mirror(QString &url) {
    const char *m = nullptr;
    if (mc_mod_curseforge_available())
        m = mc_download_effective_mirror();
    if (!m || m[0] == '\0') return;
    char translated[2048];
    if (mc_download_translate_url(url.toUtf8().constData(), translated, sizeof(translated), m))
        url = QString::fromUtf8(translated);
}

// ---- McSearchResult lifecycle ----

void mc_search_result_init(McSearchResult *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

void mc_search_result_free(McSearchResult *r) {
    if (!r) return;
    free(r->id); free(r->slug); free(r->name);
    free(r->description); free(r->logo_url);
    free(r->download_url); free(r->game_versions);
    free(r->loaders); free(r->project_type); free(r->source);
    memset(r, 0, sizeof(*r));
}

void mc_search_results_free(McSearchResult *results, int count) {
    if (!results) return;
    for (int i = 0; i < count; i++)
        mc_search_result_free(&results[i]);
}

// ---- curseforge api key / mirror ----

void mc_search_set_cf_api_key(const char *api_key) {
    mc_mod_set_curseforge_api_key(api_key);
}

void mc_search_set_mirror(const char *mirror) {
    mc_mod_set_mirror(mirror);
}

// ---- shared search implementation (Modrinth + optional CurseForge) ----

static const char *modrinth_project_type_str(int type) {
    switch (type) {
        case MC_SEARCH_TYPE_DATAPACK:  return "datapack";
        case MC_SEARCH_TYPE_SHADER:    return "shader";
        case MC_SEARCH_TYPE_RESOURCEPACK: return "resourcepack";
        default: return "mod";
    }
}

static int cf_class_for_type(int type) {
    switch (type) {
        case MC_SEARCH_TYPE_DATAPACK:  return 4472;
        case MC_SEARCH_TYPE_SHADER:    return 6552;
        case MC_SEARCH_TYPE_RESOURCEPACK: return 12;
        default: return 6;
    }
}

static int cf_source_for_type(int type) {
    switch (type) {
        case MC_SEARCH_TYPE_DATAPACK:  return MC_MOD_CURSEFORGE;
        case MC_SEARCH_TYPE_SHADER:    return MC_MOD_CURSEFORGE;
        case MC_SEARCH_TYPE_RESOURCEPACK: return MC_MOD_CURSEFORGE;
        default: return MC_MOD_CURSEFORGE;
    }
}

static int parse_modrinth_hit(const QJsonObject &obj, int type,
                              McSearchResult *r, bool fetch_primary_file) {
    r->id         = strdup_qstring(safe_string(obj, "project_id"));
    if (!r->id) r->id = strdup_qstring(safe_string(obj, "id"));
    r->slug       = strdup_qstring(safe_string(obj, "slug"));
    r->name       = strdup_qstring(safe_string(obj, "title"));
    r->description = strdup_qstring(safe_string(obj, "description"));
    r->logo_url   = strdup_qstring(safe_string(obj, "icon_url"));
    r->download_count = safe_int(obj, "downloads");
    r->project_type = strdup_qstring(modrinth_project_type_str(type));
    r->source = strdup_qstring("Modrinth");

    QStringList vers = safe_string_array(obj, "versions");
    if (!vers.isEmpty())
        r->game_versions = strdup_qstring(vers.join(", "));
    else {
        QStringList gv = safe_string_array(obj, "game_versions");
        if (!gv.isEmpty())
            r->game_versions = strdup_qstring(gv.join(", "));
    }
    QStringList loaders = safe_string_array(obj, "loaders");
    if (!loaders.isEmpty())
        r->loaders = strdup_qstring(loaders.join(", "));

    // Fetch the most recent primary file's download URL
    if (fetch_primary_file) {
        QJsonArray files = obj.value("files").toArray();
        for (auto fval : files) {
            QJsonObject fo = fval.toObject();
            // Prefer release type; otherwise pick first
            QString vt = safe_string(fo, "version_type");
            if (vt == "release" || files.size() == 1) {
                QJsonArray fa = fo.value("files").toArray();
                if (!fa.isEmpty()) {
                    QJsonObject fi = fa.first().toObject();
                    r->download_url = strdup_qstring(safe_string(fi, "url"));
                    r->size = safe_int64(fi, "size");
                }
                break;
            }
        }
    }
    return 1;
}

static int parse_cf_hit(const QJsonObject &obj, int type,
                        McSearchResult *r, bool fetch_primary_file) {
    r->id         = strdup_qstring(QString::number(safe_int(obj, "id")));
    r->slug       = strdup_qstring(safe_string(obj, "slug"));
    r->name       = strdup_qstring(safe_string(obj, "name"));
    r->description = strdup_qstring(safe_string(obj, "summary"));
    r->download_count = safe_int(obj, "downloadCount");
    r->project_type = strdup_qstring(modrinth_project_type_str(type));
    r->source = strdup_qstring("CurseForge");
    r->logo_url = strdup_qstring(safe_string(obj.value("logo").toObject(), "thumbnailUrl"));

    QStringList gv = safe_string_array(obj, "gameVersions");
    if (!gv.isEmpty())
        r->game_versions = strdup_qstring(gv.join(", "));

    QStringList loaders = safe_string_array(obj, "modLoaders");
    if (!loaders.isEmpty())
        r->loaders = strdup_qstring(loaders.join(", "));

    if (fetch_primary_file) {
        QJsonArray files = obj.value("latestFiles").toArray();
        for (auto fval : files) {
            QJsonObject fo = fval.toObject();
            QString vt_s = safe_string(fo, "releaseType");
            int rt = vt_s == "release" ? 1 : (vt_s == "beta" ? 2 : 3);
            if (rt == 1 || files.size() == 1) {
                r->download_url = strdup_qstring(safe_string(fo, "downloadUrl"));
                r->size = (long long)safe_int(fo, "fileLength");
                break;
            }
        }
    }
    return 1;
}

// Search Modrinth for a given type (datapack / shader / resourcepack)
static int search_modrinth_type(const char *query, const char *mc_version,
                                 const char *loader, int type,
                                 int limit, int offset, int sort,
                                 McSearchResult *results, int max_results) {
    QString url = QString("%1/search?limit=%2")
        .arg("https://api.modrinth.com/v2")
        .arg(qMin(limit, 50));

    if (offset > 0) url += "&offset=" + QString::number(offset);
    if (sort == MC_SEARCH_SORT_DOWNLOADS) url += "&index=downloads";
    else if (sort == MC_SEARCH_SORT_NEWEST) url += "&index=newest";
    else if (sort == MC_SEARCH_SORT_UPDATED) url += "&index=updated";
    else url += "&index=relevance";

    if (query && query[0])
        url += "&query=" + QUrl::toPercentEncoding(QString::fromUtf8(query));
    else
        url += "&query=";

    QStringList facets;
    facets << "[\"project_type:" + QString::fromUtf8(modrinth_project_type_str(type)) + "\"]";
    if (mc_version && mc_version[0])
        facets << "[\"versions:" + QString::fromUtf8(mc_version) + "\"]";
    if (loader && loader[0])
        facets << "[\"categories:" + QString::fromUtf8(loader) + "\"]";
    url += "&facets=" + QUrl::toPercentEncoding("[" + facets.join(",") + "]");

    apply_mod_mirror(url);

    HttpClient client;
    mc_http_init(&client);
    McHttpResponse *resp = mc_http_get(&client, url.toUtf8().constData());
    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    QJsonArray hits = doc.object().value("hits").toArray();
    int count = 0;
    for (auto hit : hits) {
        if (count >= max_results) break;
        QJsonObject obj = hit.toObject();
        McSearchResult *r = &results[count];
        mc_search_result_init(r);
        parse_modrinth_hit(obj, type, r, true);
        count++;
    }
    return count;
}

// Search CurseForge for a given type
static int search_curseforge_type(const char *query, const char *mc_version,
                                   const char *loader, int type,
                                   int limit, int offset, int sort,
                                   McSearchResult *results, int max_results) {
    if (!mc_mod_curseforge_available()) return 0;

    int class_id = cf_class_for_type(type);
    QString url = QString("https://api.curseforge.com/v1/mods/search?gameId=432&classId=%1&pageSize=%2&index=%3")
        .arg(class_id).arg(qMin(limit, 100)).arg(offset);

    if (query && query[0])
        url += "&searchFilter=" + QUrl::toPercentEncoding(QString::fromUtf8(query));
    if (mc_version && mc_version[0])
        url += "&gameVersion=" + QUrl::toPercentEncoding(QString::fromUtf8(mc_version));
    if (loader && loader[0]) {
        const char *lt = loader;
        if (strcmp(lt, "forge") == 0) lt = "1";
        else if (strcmp(lt, "fabric") == 0) lt = "4";
        else if (strcmp(lt, "neoforge") == 0) lt = "6";
        else lt = "1";
        url += QString("&modLoaderType=%1").arg(lt);
    }
    if (sort == MC_SEARCH_SORT_NEWEST) url += "&sortField=1";
    else if (sort == MC_SEARCH_SORT_UPDATED) url += "&sortField=2";
    else url += "&sortField=6&sortOrder=desc";

    // Mirror translate
    HttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);

    // Translate URL through mirror
    char translated_url[4096];
    strncpy(translated_url, url.toUtf8().constData(), sizeof(translated_url) - 1);
    translated_url[sizeof(translated_url) - 1] = '\0';

    // Apply modrinth-style mirror (same mechanism)
    QString qurl = url;
    apply_mod_mirror(qurl);
    strncpy(translated_url, qurl.toUtf8().constData(), sizeof(translated_url) - 1);

    char *api_hdr = nullptr;
    const char *cf_key = nullptr;
    // Access the internal CF API key via mc_mod's mirror translation path
    // We use mc_mod_get_project_cf indirectly — instead, just use the generic CF search
    // that mc_mod already handles via mc_mod_search with the right class_id

    // Reuse mc_mod's search with the right class_id by calling search_curseforge
    // But we need our own McSearchResult. Instead, let's call mc_mod_search_pack
    // with a custom approach.
    // Actually, let's just use the same pattern as mc_mod.cpp
    // Use the internal approach: call search_curseforge via the existing mc_mod functions
    // and convert results.
    (void)cf_key; (void)api_hdr;

    // Fallback: use mc_mod's internal search and convert
    McModProject *cf_results = (McModProject *)malloc(sizeof(McModProject) * (size_t)max_results);
    if (!cf_results) return 0;
    int cf_count = mc_mod_search(query, mc_version, loader,
                                  MC_MOD_CURSEFORGE, limit, offset, sort,
                                  cf_results, max_results);
    // mc_mod_search only searches mods (classId=6); we need classId for our type
    // Instead, let's just use mc_mod_search_pack which searches modpacks
    // For now, fall through to Modrinth-only path when CF key is not available
    free(cf_results);

    // Direct CF search without going through mc_mod (which is mod-specific)
    // We need to build the request ourselves
    HttpClient cf_client;
    mc_http_init(&cf_client);
    mc_http_set_timeout(&cf_client, 15000);

    // Get API key from environment or config
    const char *key_env = getenv("CF_API_KEY");
    char api_header[512];
    const char *headers[1];
    int hdr_count = 0;
    if (key_env && key_env[0]) {
        snprintf(api_header, sizeof(api_header), "x-api-key: %s", key_env);
        headers[0] = api_header;
        hdr_count = 1;
    }

    McHttpResponse *resp = hdr_count > 0
        ? mc_http_get_with_headers(&cf_client, translated_url, (const char**)headers, hdr_count)
        : mc_http_get(&cf_client, translated_url);

    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    QJsonObject root = doc.object();
    QJsonArray data = root.value("data").toArray();
    int count = 0;
    for (auto item : data) {
        if (count >= max_results) break;
        QJsonObject obj = item.toObject();
        McSearchResult *r = &results[count];
        mc_search_result_init(r);
        parse_cf_hit(obj, type, r, true);
        count++;
    }
    return count;
}

int mc_search(const char *query, const char *mc_version,
              const char *loader, int type,
              int source, int limit, int offset, int sort,
              McSearchResult *results, int max_results) {
    if (!results || max_results <= 0) return 0;
    memset(results, 0, sizeof(McSearchResult) * (size_t)max_results);

    int limit_clamped = qMin(limit, 50);

    // Try CurseForge first when available
    if ((source == MC_SEARCH_SOURCE_CURSEFORGE || source == MC_SEARCH_SOURCE_ANY)
        && mc_mod_curseforge_available()) {
        int cf = search_curseforge_type(query, mc_version, loader, type,
                                         limit_clamped, offset, sort,
                                         results, max_results);
        if (cf > 0) return cf;
        if (source == MC_SEARCH_SOURCE_CURSEFORGE) return 0;
    }

    if (source != MC_SEARCH_SOURCE_MODRINTH && source != MC_SEARCH_SOURCE_ANY)
        return 0;

    return search_modrinth_type(query, mc_version, loader, type,
                                 limit_clamped, offset, sort,
                                 results, max_results);
}
