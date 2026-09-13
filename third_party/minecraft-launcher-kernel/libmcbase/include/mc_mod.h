/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_MOD_H
#define MC_MOD_H

#include <stddef.h>

#define MC_MOD_MAX_RESULTS 100

typedef struct {
    char *id;
    char *slug;
    char *name;
    char *description;
    char *logo_url;
    int download_count;
    char *game_versions;
    char *loaders;
    int source;
    char *project_type;
    char *website_url;
} McModProject;

typedef struct {
    char *project_id;
    char *version_id;
    char *file_name;
    char *dependency_type;
} McModDependency;

typedef struct {
    char *id;
    char *project_id;
    char *display_name;
    char *file_name;
    char *download_url;
    char *sha1;
    long long size;
    char *game_versions;
    char *loaders;
    char *release_type;
    char *release_date;
    int download_count;
    McModDependency *dependencies;
    int dependency_count;
} McModFile;

enum McModSource {
    MC_MOD_MODRINTH = 1,
    MC_MOD_CURSEFORGE = 3,
    MC_MOD_ANY = 2,
};

enum McModSort {
    MC_MOD_SORT_RELEVANCE = 0,
    MC_MOD_SORT_DOWNLOADS = 1,
    MC_MOD_SORT_FOLLOWS = 2,
    MC_MOD_SORT_NEWEST = 3,
    MC_MOD_SORT_UPDATED = 4,
};

void mc_mod_project_init(McModProject *p);
void mc_mod_project_free(McModProject *p);
void mc_mod_file_init(McModFile *f);
void mc_mod_file_free(McModFile *f);

void mc_mod_set_mirror(const char *mirror);

// Set the CurseForge API key (x-api-key header). When the request is routed
// through the mcimirror relay, the key is optional (the relay proxies CF
// server-side). With no key and no relay, CurseForge is disabled and only
// Modrinth is used. The key is not persisted to disk.
void mc_mod_set_curseforge_api_key(const char *api_key);

// Warm the Modrinth mirror decision in the background (non-blocking for callers).
void mc_mod_warmup_mirror(void);

int mc_mod_search(const char *query, const char *mc_version, const char *loader,
                   int source, int limit, int offset, int sort,
                   McModProject *results, int max_results);

// Search Modrinth for modpacks (project_type=modpack). Same result struct.
// When a CurseForge API key is set (via mc_mod_set_curseforge_api_key), this
// also searches CurseForge (classId=4471) and merges both result sets.
int mc_mod_search_pack(const char *query, const char *mc_version, const char *loader,
                       int limit, int offset, int sort,
                       McModProject *results, int max_results);

int mc_mod_get_project(const char *project_id, int source,
                       McModProject *project);

// Get a CurseForge project by its numeric ID string.
int mc_mod_get_project_cf(const char *project_id, McModProject *project);

int mc_mod_get_versions(const char *project_id, int source,
                        const char *mc_version, const char *loader,
                        McModFile *files, int max_files);

// Batch project lookup (Modrinth GET /projects?ids=[...]).
int mc_mod_get_projects(const char **ids, int count,
                       McModProject *results, int max_results);

// Translate a Modrinth file/API URL to the configured mirror, if any.
int mc_mod_translate_download_url(const char *url, char *out, size_t out_size);

// Return 1 if a CurseForge API key has been configured (CF search is enabled).
int mc_mod_curseforge_available(void);

#endif
