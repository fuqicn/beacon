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
#include <cctype>

static char g_mirror[64] = "";
static char g_cf_api_key[256] = "";

static const char *MR_BASE = "https://api.modrinth.com/v2";
static const char *CF_BASE = "https://api.curseforge.com/v1";
static const int CF_GAME_ID = 432;
static const int CF_CLASS_MOD = 6;
static const int CF_CLASS_MODPACK = 4471;

// Modrinth mirror decision for auto mode. Warmed up in the background by
// mc_mod_warmup_mirror(); empty means "use the direct cdn.modrinth.com".
static char g_mod_auto_mirror[64] = "";
static std::atomic<int> g_mod_auto_ready{0};
static std::mutex g_mod_auto_mutex;

// Probe the direct Modrinth CDN against the mcimirror proxy and pick whichever
// is reachable and fastest. Any HTTP response counts as reachable.
static void warmup_modrinth_probe(void) {
    const char *direct_url = "https://cdn.modrinth.com/";
    const char *mirror_url = "https://mod.mcimirror.top/";

    HttpClient c;
    mc_http_init(&c);
    mc_http_set_timeout(&c, 3000);

    auto t1 = std::chrono::steady_clock::now();
    McHttpResponse *rd = mc_http_head(&c, direct_url);
    auto t2 = std::chrono::steady_clock::now();
    bool direct_ok = rd && rd->success;
    double direct_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    if (rd) mc_http_response_free(rd);

    auto t3 = std::chrono::steady_clock::now();
    McHttpResponse *rm = mc_http_head(&c, mirror_url);
    auto t4 = std::chrono::steady_clock::now();
    bool mirror_ok = rm && rm->success;
    double mirror_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();
    if (rm) mc_http_response_free(rm);

    const char *pick = (mirror_ok && (!direct_ok || mirror_ms < direct_ms)) ? "mcimirror" : "";
    {
        std::lock_guard<std::mutex> lk(g_mod_auto_mutex);
        strncpy(g_mod_auto_mirror, pick, sizeof(g_mod_auto_mirror) - 1);
        g_mod_auto_mirror[sizeof(g_mod_auto_mirror) - 1] = '\0';
        g_mod_auto_ready.store(1);
    }
    mc_info("Modrinth mirror probe: direct=%s(%.0fms) mirror=%s(%.0fms) -> %s",
            direct_ok ? "ok" : "fail", direct_ms,
            mirror_ok ? "ok" : "fail", mirror_ms,
            g_mod_auto_mirror[0] ? "mcimirror" : "direct");
}

// Warm the Modrinth mirror decision in the background. Safe to call from any
// thread; it only touches its own state plus the shared 10-minute mirror cache.
void mc_mod_warmup_mirror(void) {
    (void)mc_download_effective_mirror();
    warmup_modrinth_probe();
}

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

// mirror mapping �?delegates to the generic mirror translator
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

void mc_mod_set_curseforge_api_key(const char *api_key) {
    if (api_key && api_key[0]) {
        strncpy(g_cf_api_key, api_key, sizeof(g_cf_api_key) - 1);
        g_cf_api_key[sizeof(g_cf_api_key) - 1] = '\0';
        mc_info("CurseForge API key configured");
    } else {
        g_cf_api_key[0] = '\0';
        mc_info("CurseForge API key cleared");
    }
}

int mc_mod_curseforge_available(void) {
    return g_cf_api_key[0] ? 1 : 0;
}

static McHttpResponse *http_get_json(const char *url) {
    HttpClient client;
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

// Forward declarations for CurseForge helpers defined later
static int search_curseforge(const char *query, const char *mc_version, const char *loader,
                             int limit, int offset, int sort, int class_id,
                             McModProject *results, int max_results);

int mc_mod_search(const char *query, const char *mc_version, const char *loader,
                  int source, int limit, int offset, int sort,
                  McModProject *results, int max_results) {
    if (!results || max_results <= 0) return 0;

    // CurseForge first (when requested or in ANY mode with a key set)
    if ((source == MC_MOD_CURSEFORGE || source == MC_MOD_ANY) && g_cf_api_key[0]) {
        int cf = search_curseforge(query, mc_version, loader, limit, offset, sort,
                                    CF_CLASS_MOD, results, max_results);
        if (cf > 0) return cf;
        // fall through to Modrinth as backup
        if (source == MC_MOD_CURSEFORGE) return 0;
    }

    if (source != MC_MOD_MODRINTH && source != MC_MOD_ANY)
        return 0;

    return search_modrinth(query, mc_version, loader, limit, offset, sort, "mod",
                           results, max_results);
}

int mc_mod_search_pack(const char *query, const char *mc_version, const char *loader,
                       int limit, int offset, int sort,
                       McModProject *results, int max_results) {
    if (!results || max_results <= 0) return 0;

    // CurseForge modpacks (classId=4471) when a key is configured.
    if (g_cf_api_key[0]) {
        int cf = search_curseforge(query, mc_version, loader, limit, offset, sort,
                                    CF_CLASS_MODPACK, results, max_results);
        if (cf > 0) return cf;
    }

    return search_modrinth(query, mc_version, loader, limit, offset, sort, "modpack",
                           results, max_results);
}

// ---- CurseForge search ----
static int get_versions_cf(const char *project_id, const char *mc_version, const char *loader,
                           McModFile *files, int max_files);
static McHttpResponse *cf_http_get(const char *url, const char *api_key) {
    HttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 15000);
    char *api_hdr = (char *)malloc(strlen(api_key) + 16);
    char *hdrs[2];
    int hdr_cnt = 1;
    snprintf(api_hdr, 16, "x-api-key: %s", api_key);
    hdrs[0] = api_hdr;
    McHttpResponse *resp = mc_http_get_with_headers(&client, url, (const char**)hdrs, hdr_cnt);
    free(api_hdr);
    return resp;
}

static const char *cf_class_name(int classId) {
    if (classId == CF_CLASS_MOD) return "mod";
    if (classId == CF_CLASS_MODPACK) return "modpack";
    if (classId == 12) return "resourcepack";
    if (classId == 4472) return "datapack";
    if (classId == 6552) return "shader";
    return "mod";
}

static int search_curseforge(const char *query, const char *mc_version, const char *loader,
                             int limit, int offset, int sort, int class_id,
                             McModProject *results, int max_results) {
    if (!g_cf_api_key[0]) {
        mc_warn("CurseForge requires --cfapi <key>; skipping");
        return 0;
    }
    QString url = QString("%1/mods/search?gameId=%2&classId=%3&pageSize=%4&index=%5")
        .arg(CF_BASE).arg(CF_GAME_ID).arg(class_id)
        .arg(qMin(limit, 100)).arg(offset);

    if (query && query[0])
        url += "&searchFilter=" + QUrl::toPercentEncoding(QString::fromUtf8(query));
    if (mc_version && mc_version[0])
        url += "&gameVersion=" + QUrl::toPercentEncoding(QString::fromUtf8(mc_version));
    if (loader && loader[0]) {
        const char *lt = loader;
        if (strcmp(lt, "forge") == 0) lt = "1";
        else if (strcmp(lt, "cauldron") == 0) lt = "2";
        else if (strcmp(lt, "liteloader") == 0) lt = "3";
        else if (strcmp(lt, "fabric") == 0) lt = "4";
        else if (strcmp(lt, "quilt") == 0) lt = "5";
        else if (strcmp(lt, "neoforge") == 0) lt = "6";
        else lt = "1";
        url += QString("&modLoaderType=%1").arg(lt);
    }
    url += "&sortField=6&sortOrder=desc";
    if (sort == MC_MOD_SORT_RELEVANCE)
        url.replace("&sortField=6&sortOrder=desc", "&sortOrder=desc");
    if (sort == MC_MOD_SORT_NEWEST)
        url += "&sortField=1";
    if (sort == MC_MOD_SORT_UPDATED)
        url += "&sortField=2";

    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = cf_http_get(qurl.toUtf8().constData(), g_cf_api_key);
    if (!resp || !resp->success || resp->status_code != 200) {
        mc_warn("CurseForge search failed: %s",
                resp ? (resp->error[0] ? resp->error : "non-200") : "no response");
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) {
        mc_warn("CurseForge search: bad JSON");
        return 0;
    }

    QJsonObject root = doc.object();
    QJsonArray data = root.value("data").toArray();
    int count = 0;
    for (auto item : data) {
        if (count >= max_results) break;
        QJsonObject obj = item.toObject();
        McModProject *p = &results[count];
        mc_mod_project_init(p);
        p->id = strdup_qstring(QString::number(safe_int(obj, "id")));
        p->slug = strdup_qstring(safe_string(obj, "slug"));
        p->name = strdup_qstring(safe_string(obj, "name"));
        p->description = strdup_qstring(safe_string(obj, "summary"));
        p->download_count = safe_int(obj, "downloadCount");
        p->source = MC_MOD_CURSEFORGE;
        p->project_type = strdup_qstring(QString::fromUtf8(cf_class_name(safe_int(obj, "classId"))));
        p->logo_url = strdup_qstring(safe_string(obj.value("logo").toObject(), "thumbnailUrl"));
        p->website_url = strdup_qstring(safe_string(obj.value("links").toObject(), "websiteUrl"));

        QStringList gv = safe_string_array(obj, "gameVersions");
        if (!gv.isEmpty())
            p->game_versions = strdup_qstring(gv.join(", "));

        QStringList loaders = safe_string_array(obj, "modLoaders");
        if (!loaders.isEmpty())
            p->loaders = strdup_qstring(loaders.join(", "));

        count++;
    }
    return count;
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

int mc_mod_get_project_cf(const char *project_id, McModProject *project) {
    if (!project_id || !project || !g_cf_api_key[0]) return 0;
    mc_mod_project_init(project);

    QString url = QString("%1/mods/%2").arg(CF_BASE).arg(project_id);
    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = cf_http_get(qurl.toUtf8().constData(), g_cf_api_key);
    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    QJsonObject obj = doc.object().value("data").toObject();
    project->id = strdup_qstring(QString::number(safe_int(obj, "id")));
    project->slug = strdup_qstring(safe_string(obj, "slug"));
    project->name = strdup_qstring(safe_string(obj, "name"));
    project->description = strdup_qstring(safe_string(obj, "summary"));
    project->download_count = safe_int(obj, "downloadCount");
    project->source = MC_MOD_CURSEFORGE;
    project->project_type = strdup_qstring(QString::fromUtf8(cf_class_name(safe_int(obj, "classId"))));
    project->logo_url = strdup_qstring(safe_string(obj.value("logo").toObject(), "thumbnailUrl"));
    project->website_url = strdup_qstring(safe_string(obj.value("links").toObject(), "websiteUrl"));

    QStringList gv = safe_string_array(obj, "gameVersions");
    if (!gv.isEmpty())
        project->game_versions = strdup_qstring(gv.join(", "));

    return 1;
}

int mc_mod_get_versions(const char *project_id, int source,
                       const char *mc_version, const char *loader,
                       McModFile *files, int max_files) {
    (void)source;
    if (!project_id || !files || max_files <= 0) return 0;

    if (source == MC_MOD_CURSEFORGE || (source == MC_MOD_ANY && g_cf_api_key[0] && isdigit(project_id[0]))) {
        return get_versions_cf(project_id, mc_version, loader, files, max_files);
    }

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

static int get_versions_cf(const char *project_id, const char *mc_version, const char *loader,
                           McModFile *files, int max_files) {
    if (!g_cf_api_key[0]) return 0;
    QString url = QString("%1/mods/%2/files?pageSize=100").arg(CF_BASE).arg(project_id);
    if (mc_version && mc_version[0])
        url += "&gameVersion=" + QUrl::toPercentEncoding(QString::fromUtf8(mc_version));
    if (loader && loader[0]) {
        const char *lt = loader;
        if (strcmp(lt, "forge") == 0) lt = "1";
        else if (strcmp(lt, "cauldron") == 0) lt = "2";
        else if (strcmp(lt, "liteloader") == 0) lt = "3";
        else if (strcmp(lt, "fabric") == 0) lt = "4";
        else if (strcmp(lt, "quilt") == 0) lt = "5";
        else if (strcmp(lt, "neoforge") == 0) lt = "6";
        else lt = "1";
        url += QString("&modLoaderType=%1").arg(lt);
    }
    QString qurl = url;
    apply_mirror(qurl);

    McHttpResponse *resp = cf_http_get(qurl.toUtf8().constData(), g_cf_api_key);
    if (!resp || !resp->success || resp->status_code != 200) {
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data, (int)resp->data_len), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) return 0;

    QJsonArray data = doc.object().value("data").toArray();
    int count = 0;
    for (auto item : data) {
        if (count >= max_files) break;
        QJsonObject obj = item.toObject();

        McModFile *f = &files[count];
        mc_mod_file_init(f);
        f->id = strdup_qstring(QString::number(safe_int(obj, "id")));
        f->project_id = strdup_qstring(project_id);
        f->display_name = strdup_qstring(safe_string(obj, "displayName"));
        f->file_name = strdup_qstring(safe_string(obj, "fileName"));
        f->download_url = strdup_qstring(safe_string(obj, "downloadUrl"));
        f->size = (long long)safe_int(obj, "fileLength");
        f->download_count = safe_int(obj, "downloadCount");
        f->release_date = strdup_qstring(safe_string(obj, "fileDate"));

        int rt = safe_int(obj, "releaseType");
        if (rt == 1) f->release_type = strdup_qstring("release");
        else if (rt == 2) f->release_type = strdup_qstring("beta");
        else if (rt == 3) f->release_type = strdup_qstring("alpha");
        else f->release_type = strdup_qstring("release");

        QStringList gv = safe_string_array(obj, "gameVersions");
        if (!gv.isEmpty())
            f->game_versions = strdup_qstring(gv.join(", "));

        QJsonArray loadersArr = obj.value("modLoaders").toArray();
        QStringList loaderNames;
        for (auto lv : loadersArr) {
            QJsonObject lo = lv.toObject();
            if (!safe_string(lo, "name").isEmpty())
                loaderNames << safe_string(lo, "name");
        }
        if (!loaderNames.isEmpty())
            f->loaders = strdup_qstring(loaderNames.join(", "));

        QJsonArray hashes = obj.value("hashes").toArray();
        for (auto h : hashes) {
            QJsonObject ho = h.toObject();
            if (safe_int(ho, "algorithm") == 1) {
                f->sha1 = strdup_qstring(safe_string(ho, "value"));
                break;
            }
        }

        QJsonArray deps = obj.value("dependencies").toArray();
        if (!deps.isEmpty()) {
            f->dependency_count = deps.size();
            f->dependencies = (McModDependency *)malloc((size_t)deps.size() * sizeof(McModDependency));
            if (f->dependencies) {
                memset(f->dependencies, 0, (size_t)deps.size() * sizeof(McModDependency));
                for (int j = 0; j < deps.size(); j++) {
                    QJsonObject d = deps[j].toObject();
                    f->dependencies[j].project_id = strdup_qstring(QString::number(safe_int(d, "modId")));
                    int rtype = safe_int(d, "relationType");
                    const char *dep_type = "required";
                    if (rtype == 1) dep_type = "embedded";
                    else if (rtype == 2) dep_type = "optional";
                    else if (rtype == 3) dep_type = "required";
                    else if (rtype == 5) dep_type = "incompatible";
                    else if (rtype == 6) dep_type = "include";
                    f->dependencies[j].dependency_type = strdup_qstring(dep_type);
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
