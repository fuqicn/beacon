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

// ---- McSearchFile lifecycle ----

void mc_search_file_free(McSearchFile *f) {
    if (!f) return;
    free(f->version_id); free(f->file_id); free(f->file_name);
    free(f->download_url); free(f->sha1);
    free(f->version_type); free(f->date_published);
    memset(f, 0, sizeof(*f));
}

void mc_search_files_free(McSearchFile *files, int count) {
    if (!files) return;
    for (int i = 0; i < count; i++)
        mc_search_file_free(&files[i]);
}

// ---- source / mirror config ----

void mc_search_set_cf_api_key(const char *api_key) {
    mc_mod_set_curseforge_api_key(api_key);
}

void mc_search_set_mirror(const char *mirror) {
    mc_mod_set_mirror(mirror);
}

// ---- type → strings ----

static const char *modrinth_project_type_str(int type) {
    switch (type) {
        case MC_SEARCH_TYPE_DATAPACK:       return "datapack";
        case MC_SEARCH_TYPE_SHADER:         return "shader";
        case MC_SEARCH_TYPE_RESOURCEPACK:   return "resourcepack";
        default: return "mod";
    }
}

static int cf_class_for_type(int type) {
    switch (type) {
        case MC_SEARCH_TYPE_DATAPACK:       return 4472;
        case MC_SEARCH_TYPE_SHADER:         return 6552;
        case MC_SEARCH_TYPE_RESOURCEPACK:   return 12;
        default: return 6;
    }
}

// ---- Modrinth search ----

static int search_modrinth_type(const char *query, const char *mc_version,
                                 const char *loader, int type,
                                 int limit, int offset, int sort,
                                 McSearchResult *results, int max_results) {
    QString url = QString("https://api.modrinth.com/v2/search?limit=%1")
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

        r->id         = strdup_qstring(safe_string(obj, "project_id"));
        if (!r->id) r->id = strdup_qstring(safe_string(obj, "id"));
        r->slug       = strdup_qstring(safe_string(obj, "slug"));
        r->name       = strdup_qstring(safe_string(obj, "title"));
        r->description = strdup_qstring(safe_string(obj, "description"));
        r->logo_url   = strdup_qstring(safe_string(obj, "icon_url"));
        r->download_count = safe_int(obj, "downloads");
        r->project_type = strdup_qstring(modrinth_project_type_str(type));
        r->source     = strdup_qstring("Modrinth");

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

        // Modrinth search results do not include file artifacts. Build a
        // best-effort CDN URL from project_id + latest_version so the
        // consumer has at least a link to follow.
        // Pattern: https://cdn.modrinth.com/data/{project_id}/versions/{version_id}/
        QString latest_ver = safe_string(obj, "latest_version");
        if (!latest_ver.isEmpty() && r->id && r->id[0]) {
            r->download_url = strdup_qstring(
                QString("https://cdn.modrinth.com/data/%1/versions/%2/")
                    .arg(r->id, latest_ver).toUtf8().constData());
        }
        count++;
    }
    return count;
}

// ---- CurseForge search (delegates to mc_mod_search_class) ----

static int search_curseforge_type(const char *query, const char *mc_version,
                                    const char *loader, int type,
                                    int limit, int offset, int sort,
                                    McSearchResult *results, int max_results) {
    if (!mc_mod_curseforge_available()) return 0;

    int class_id = cf_class_for_type(type);
    McModProject *cf_results = (McModProject *)malloc(
        sizeof(McModProject) * (size_t)max_results);
    if (!cf_results) return 0;
    int cf_count = mc_mod_search_class(query, mc_version, loader,
                                        class_id, limit, offset, sort,
                                        cf_results, max_results);
    if (cf_count <= 0) { free(cf_results); return 0; }

    int count = 0;
    for (int i = 0; i < cf_count; i++) {
        McSearchResult *r = &results[count];
        McModProject *p = &cf_results[i];

        mc_search_result_init(r);
        r->id         = strdup_qstring(p->id);
        r->slug       = strdup_qstring(p->slug);
        r->name       = strdup_qstring(p->name);
        r->description = strdup_qstring(p->description);
        r->logo_url   = strdup_qstring(p->logo_url);
        r->download_count = p->download_count;
        const char *pt = modrinth_project_type_str(class_id == 12 ? MC_SEARCH_TYPE_RESOURCEPACK
                                                                   : class_id == 4472 ? MC_SEARCH_TYPE_DATAPACK
                                                                   : class_id == 6552 ? MC_SEARCH_TYPE_SHADER
                                                                                      : MC_SEARCH_TYPE_DATAPACK);
        r->project_type = strdup_qstring(pt);
        r->source     = strdup_qstring("CurseForge");
        r->game_versions = strdup_qstring(p->game_versions);
        r->loaders   = strdup_qstring(p->loaders);

        // Fetch primary file download URL via mc_mod_get_versions
        McModFile files[20];
        int fc = mc_mod_get_versions(p->id, MC_MOD_CURSEFORGE,
                                      mc_version, loader, files, 20);
        for (int j = 0; j < fc; j++) {
            if (files[j].release_type &&
                (strcmp(files[j].release_type, "release") == 0 || fc == 1)) {
                r->download_url = strdup_qstring(files[j].download_url);
                r->size = files[j].size;
                break;
            }
        }
        if (!r->download_url && fc > 0) {
            r->download_url = strdup_qstring(files[0].download_url);
            r->size = files[0].size;
        }
        for (int j = 0; j < fc; j++)
            mc_mod_file_free(&files[j]);

        count++;
    }

    for (int i = 0; i < cf_count; i++)
        mc_mod_project_free(&cf_results[i]);
    free(cf_results);
    return count;
}

// ---- Public API ----

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

// ---- mc_search_get_files implementation ----
// Fetches the version list for a Modrinth project and resolves the primary
// file download URL for each entry. Mirrors the same logic as
// mc_mod_get_versions in mc_mod.cpp but returns McSearchFile structs.

int mc_search_get_files(const char *project_id,
                        const char *mc_version, const char *loader,
                        McSearchFile *out, int max_files) {
    if (!project_id || !out || max_files <= 0) return 0;

    QString url = QString("https://api.modrinth.com/v2/project/%1/version")
        .arg(QString::fromUtf8(project_id));
    apply_mod_mirror(url);

    HttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);
    McHttpResponse *resp = mc_http_get(&client, url.toUtf8().constData());
    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    int count = 0;
    QJsonArray data = doc.array();
    for (auto item : data) {
        if (count >= max_files) break;
        QJsonObject obj = item.toObject();

        // Filter by MC version
        if (mc_version && mc_version[0]) {
            QJsonArray gv = obj.value("game_versions").toArray();
            bool match = false;
            for (auto v : gv) {
                if (v.toString() == QString::fromUtf8(mc_version)) {
                    match = true; break;
                }
            }
            if (!match) continue;
        }

        // Filter by loader
        if (loader && loader[0]) {
            QJsonArray ld = obj.value("loaders").toArray();
            bool match = false;
            for (auto v : ld) {
                if (v.toString() == QString::fromUtf8(loader)) {
                    match = true; break;
                }
            }
            if (!match) continue;
        }

        McSearchFile *f = &out[count];
        memset(f, 0, sizeof(*f));
        f->version_id   = strdup_qstring(safe_string(obj, "id"));
        f->file_name    = strdup_qstring(safe_string(obj, "name"));
        f->version_type = strdup_qstring(safe_string(obj, "version_type"));
        f->date_published = strdup_qstring(safe_string(obj, "date_published"));
        f->is_primary   = 0;

        // Grab the primary file entry from this version
        QJsonArray fileArr = obj.value("files").toArray();
        for (auto fv : fileArr) {
            QJsonObject fi = fv.toObject();
            bool primary = safe_int(fi, "primary");
            if (primary) f->is_primary = 1;
            if (!f->file_id)
                f->file_id   = strdup_qstring(safe_string(fi, "id"));
            if (!f->download_url)
                f->download_url = strdup_qstring(safe_string(fi, "url"));
            if (!f->sha1)
                f->sha1 = strdup_qstring(
                    safe_string(fi.value("hashes").toObject(), "sha1"));
            if (!f->file_name || !f->file_name[0])
                f->file_name = strdup_qstring(safe_string(fi, "filename"));
            if (!f->size)
                f->size = safe_int64(fi, "size");
        }

        // Fallback: use latest_version as a CDN hint if no file entry had a URL
        if (!f->download_url) {
            QString lv = safe_string(obj, "id"); // version id itself
            if (!lv.isEmpty()) {
                f->download_url = strdup_qstring(
                    QString("https://cdn.modrinth.com/data/%1/versions/%2/")
                        .arg(QString::fromUtf8(project_id), lv)
                        .toUtf8().constData());
            }
        }

        count++;
    }
    return count;
}
