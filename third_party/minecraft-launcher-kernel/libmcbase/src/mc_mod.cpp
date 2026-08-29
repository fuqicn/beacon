/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_mod.h"
#include "mc_http.h"
#include "mc_download.h"
#include "mc_log.h"

#include <atomic>
#include <chrono>
#include <mutex>

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

static char g_mirror[64] = "";

static const char *MR_BASE = "https://api.modrinth.com/v2";

// Modrinth mirror decision for auto mode. Warmed up in the background by
// mc_mod_warmup_mirror(); empty means "use the direct cdn.modrinth.com".
static char g_mod_auto_mirror[64] = "";
static std::atomic<int> g_mod_auto_ready{0};
static std::mutex g_mod_auto_mutex;

// Resolve the effective mirror for Modrinth traffic without ever blocking on
// network I/O: explicit choices are returned verbatim, "auto" uses the
// background-warmed decision and falls back to direct until it is ready.
static const char *mod_effective_mirror(void) {
    if (g_mirror[0] && strcmp(g_mirror, "auto") != 0)
        return g_mirror;
    if (!g_mod_auto_ready.load())
        return "";
    std::lock_guard<std::mutex> lk(g_mod_auto_mutex);
    return g_mod_auto_mirror;
}

// mirror mapping — delegates to the generic mirror translator
static void apply_mirror(QString &url) {
    const char *eff = mod_effective_mirror();
    if (!eff || eff[0] == '\0') return;
    char translated[2048];
    if (mc_download_translate_url(url.toUtf8().constData(), translated, sizeof(translated), eff))
        url = QString::fromUtf8(translated);
}

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

static QString safe_string(const QJsonObject &obj, const char *key) {
    return obj.value(key).toString();
}

static QStringList safe_string_array(const QJsonObject &obj, const char *key) {
    QStringList r;
    QJsonArray a = obj.value(key).toArray();
    for (auto v : a) r.append(v.toString());
    return r;
}

void mc_mod_project_init(McModProject *p) {
    memset(p, 0, sizeof(*p));
}

void mc_mod_project_free(McModProject *p) {
    if (!p) return;
    free(p->id); free(p->slug); free(p->name); free(p->description);
    free(p->logo_url); free(p->game_versions); free(p->loaders);
    free(p->project_type); free(p->website_url);
    memset(p, 0, sizeof(*p));
}

void mc_mod_file_init(McModFile *f) {
    memset(f, 0, sizeof(*f));
}

void mc_mod_file_free(McModFile *f) {
    if (!f) return;
    free(f->id); free(f->project_id); free(f->display_name);
    free(f->file_name); free(f->download_url); free(f->sha1);
    free(f->game_versions); free(f->loaders); free(f->release_type);
    free(f->release_date);
    for (int i = 0; i < f->dependency_count; i++) {
        free(f->dependencies[i].project_id);
        free(f->dependencies[i].version_id);
        free(f->dependencies[i].file_name);
        free(f->dependencies[i].dependency_type);
    }
    free(f->dependencies);
    memset(f, 0, sizeof(*f));
}

void mc_mod_set_mirror(const char *mirror) {
    if (mirror) {
        strncpy(g_mirror, mirror, sizeof(g_mirror) - 1);
        g_mirror[sizeof(g_mirror) - 1] = '\0';
    }
}

static McHttpResponse *http_get_json(const char *url) {
    McHttpClient client;
    mc_http_init(&client);
    return mc_http_get(&client, url);
}

// ---- Modrinth search ----
static int search_modrinth(const char *query, const char *mc_version, const char *loader,
                           int limit, int offset, int sort, const char *project_type,
                           McModProject *results, int max_results) {
    QString url = QString("%1/search?limit=%2")
        .arg(MR_BASE).arg(qMin(limit, 50));

    if (offset > 0)
        url += "&offset=" + QString::number(offset);

    if (sort == MC_MOD_SORT_DOWNLOADS) url += "&index=downloads";
    else if (sort == MC_MOD_SORT_FOLLOWS) url += "&index=follows";
    else if (sort == MC_MOD_SORT_NEWEST) url += "&index=newest";
    else if (sort == MC_MOD_SORT_UPDATED) url += "&index=updated";
    else url += "&index=relevance";

    if (query && query[0])
        url += "&query=" + QUrl::toPercentEncoding(QString::fromUtf8(query));
    else
        url += "&query=";

    // Filter by project type / game version / loader at search time
    // (Modrinth facets). Note: the top-level "project_type" query parameter is
    // silently ignored by Modrinth when "query" is empty, so we always filter
    // through facets and double-check each hit client-side below.
    QStringList facets;
    if (project_type && project_type[0])
        facets << "[\"project_type:" + QString::fromUtf8(project_type) + "\"]";
    if (mc_version && mc_version[0])
        facets << "[\"versions:" + QString::fromUtf8(mc_version) + "\"]";
    if (loader && loader[0])
        facets << "[\"categories:" + QString::fromUtf8(loader) + "\"]";
    if (!facets.isEmpty())
        url += "&facets=" + QUrl::toPercentEncoding("[" + facets.join(",") + "]");

    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = http_get_json(qurl.toUtf8().constData());
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

        // Defense in depth: Modrinth can ignore the project_type facet for
        // empty queries; drop any hit whose type does not match.
        if (project_type && project_type[0]) {
            const QString hitType = safe_string(obj, "project_type");
            if (!hitType.isEmpty() && hitType != QString::fromUtf8(project_type))
                continue;
        }

        McModProject *p = &results[count];
        mc_mod_project_init(p);
        p->id = strdup_qstring(safe_string(obj, "project_id"));
        if (!p->id) p->id = strdup_qstring(safe_string(obj, "id"));
        p->slug = strdup_qstring(safe_string(obj, "slug"));
        p->name = strdup_qstring(safe_string(obj, "title"));
        p->description = strdup_qstring(safe_string(obj, "description"));
        p->download_count = safe_int(obj, "downloads");
        p->source = MC_MOD_MODRINTH;
        p->logo_url = strdup_qstring(safe_string(obj, "icon_url"));
        p->project_type = strdup_qstring(safe_string(obj, "project_type"));

        QStringList vers = safe_string_array(obj, "versions");
        if (!vers.isEmpty())
            p->game_versions = strdup_qstring(vers.join(", "));

        QStringList loaders = safe_string_array(obj, "loaders");
        if (!loaders.isEmpty())
            p->loaders = strdup_qstring(loaders.join(", "));

        count++;
    }
    return count;
}

int mc_mod_search(const char *query, const char *mc_version, const char *loader,
                  int source, int limit, int offset, int sort,
                  McModProject *results, int max_results) {
    if (!results || max_results <= 0) return 0;

    if (source != MC_MOD_MODRINTH && source != MC_MOD_ANY)
        return 0;

    return search_modrinth(query, mc_version, loader, limit, offset, sort, "mod",
                           results, max_results);
}

int mc_mod_search_pack(const char *query, const char *mc_version, const char *loader,
                        int limit, int offset, int sort,
                        McModProject *results, int max_results) {
    if (!results || max_results <= 0) return 0;
    return search_modrinth(query, mc_version, loader, limit, offset, sort, "modpack",
                           results, max_results);
}

int mc_mod_get_project(const char *project_id, int source,
                       McModProject *project) {
    (void)source;
    if (!project_id || !project) return 0;
    mc_mod_project_init(project);

    QString url = QString("%1/project/%2").arg(MR_BASE).arg(project_id);
    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = http_get_json(qurl.toUtf8().constData());
    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    QJsonObject obj = doc.object();
    project->id = strdup_qstring(safe_string(obj, "id"));
    project->slug = strdup_qstring(safe_string(obj, "slug"));
    project->name = strdup_qstring(safe_string(obj, "title"));
    project->description = strdup_qstring(safe_string(obj, "description"));
    project->download_count = safe_int(obj, "downloads");
    project->source = MC_MOD_MODRINTH;
    project->logo_url = strdup_qstring(safe_string(obj, "icon_url"));
    project->website_url = strdup_qstring(safe_string(obj, "website_url"));

    QStringList gv = safe_string_array(obj, "game_versions");
    if (!gv.isEmpty())
        project->game_versions = strdup_qstring(gv.join(", "));
    else {
        QStringList vers = safe_string_array(obj, "versions");
        if (!vers.isEmpty())
            project->game_versions = strdup_qstring(vers.join(", "));
    }

    QStringList loaders = safe_string_array(obj, "loaders");
    if (!loaders.isEmpty())
        project->loaders = strdup_qstring(loaders.join(", "));

    return 1;
}

int mc_mod_get_versions(const char *project_id, int source,
                        const char *mc_version, const char *loader,
                        McModFile *files, int max_files) {
    (void)source;
    if (!project_id || !files || max_files <= 0) return 0;

    QString url = QString("%1/project/%2/version").arg(MR_BASE).arg(project_id);
    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = http_get_json(qurl.toUtf8().constData());
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

        McModFile *f = &files[count];
        mc_mod_file_init(f);
        f->id = strdup_qstring(safe_string(obj, "id"));
        f->project_id = strdup_qstring(QString::fromUtf8(project_id));
        f->display_name = strdup_qstring(safe_string(obj, "name"));
        f->release_type = strdup_qstring(safe_string(obj, "version_type"));
        f->release_date = strdup_qstring(safe_string(obj, "date_published"));
        f->download_count = safe_int(obj, "downloads");

        QJsonArray fileArr = obj.value("files").toArray();
        if (!fileArr.isEmpty()) {
            QJsonObject fObj = fileArr.first().toObject();
            f->file_name = strdup_qstring(safe_string(fObj, "filename"));
            f->sha1 = strdup_qstring(safe_string(fObj.value("hashes").toObject(), "sha1"));
            f->size = (long long)safe_int(fObj, "size");
            f->download_url = strdup_qstring(safe_string(fObj, "url"));
        }

        QStringList loadersArr = safe_string_array(obj, "loaders");
        if (!loadersArr.isEmpty())
            f->loaders = strdup_qstring(loadersArr.join(", "));

        QStringList gvArr = safe_string_array(obj, "game_versions");
        if (!gvArr.isEmpty())
            f->game_versions = strdup_qstring(gvArr.join(", "));

        QJsonArray deps = obj.value("dependencies").toArray();
        if (!deps.isEmpty()) {
            f->dependency_count = deps.size();
            f->dependencies = (McModDependency *)malloc((size_t)deps.size() * sizeof(McModDependency));
            if (f->dependencies) {
                memset(f->dependencies, 0, (size_t)deps.size() * sizeof(McModDependency));
                for (int j = 0; j < deps.size(); j++) {
                    QJsonObject d = deps[j].toObject();
                    f->dependencies[j].project_id = strdup_qstring(safe_string(d, "project_id"));
                    f->dependencies[j].version_id = strdup_qstring(safe_string(d, "version_id"));
                    f->dependencies[j].file_name = strdup_qstring(safe_string(d, "file_name"));
                    f->dependencies[j].dependency_type = strdup_qstring(safe_string(d, "dependency_type"));
                }
            }
        }

        count++;
    }

    return count;
}

int mc_mod_get_projects(const char **ids, int count,
                        McModProject *results, int max_results) {
    if (!ids || count <= 0 || !results || max_results <= 0) return 0;

    QStringList encoded;
    int n = qMin(count, max_results);
    for (int i = 0; i < n; i++) {
        if (!ids[i] || !ids[i][0]) continue;
        QString v = QString::fromUtf8(ids[i]);
        v.replace("\\", "\\\\").replace("\"", "\\\"");
        encoded << "\"" + v + "\"";
    }
    if (encoded.isEmpty()) return 0;

    QString url = QString("%1/projects?ids=[%2]").arg(MR_BASE).arg(encoded.join(","));
    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = http_get_json(qurl.toUtf8().constData());
    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    int count2 = 0;
    QJsonArray arr = doc.array();
    for (auto item : arr) {
        if (count2 >= max_results) break;
        QJsonObject obj = item.toObject();
        McModProject *p = &results[count2];
        mc_mod_project_init(p);
        p->id = strdup_qstring(safe_string(obj, "id"));
        p->slug = strdup_qstring(safe_string(obj, "slug"));
        p->name = strdup_qstring(safe_string(obj, "title"));
        p->description = strdup_qstring(safe_string(obj, "description"));
        p->download_count = safe_int(obj, "downloads");
        p->source = MC_MOD_MODRINTH;
        p->logo_url = strdup_qstring(safe_string(obj, "icon_url"));
        p->project_type = strdup_qstring(safe_string(obj, "project_type"));
        p->website_url = strdup_qstring(safe_string(obj, "website_url"));

        QStringList gv = safe_string_array(obj, "game_versions");
        if (!gv.isEmpty())
            p->game_versions = strdup_qstring(gv.join(", "));

        QStringList loaders = safe_string_array(obj, "loaders");
        if (!loaders.isEmpty())
            p->loaders = strdup_qstring(loaders.join(", "));

        count2++;
    }
    return count2;
}

int mc_mod_translate_download_url(const char *url, char *out, size_t out_size) {
    if (!url || !out || out_size == 0) return 0;
    QString s = QString::fromUtf8(url);
    QString orig = s;
    apply_mirror(s);
    if (s == orig) return 0;
    QByteArray ba = s.toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size());
    out[ba.size()] = '\0';
    return 1;
}

void mc_mod_warmup_mirror(void)
{
    McMirrorProbe probes[MC_MAX_MIRROR_TYPES];
    int count = mc_mirror_probe_all(probes, MC_MAX_MIRROR_TYPES);
    const char *best = mc_mirror_select_best(probes, count);
    if (!best) best = "mojang";
    std::lock_guard<std::mutex> lk(g_mod_auto_mutex);
    strncpy(g_mod_auto_mirror, best, sizeof(g_mod_auto_mirror) - 1);
    g_mod_auto_mirror[sizeof(g_mod_auto_mirror) - 1] = '\0';
    g_mod_auto_ready.store(1);
}
