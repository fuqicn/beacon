/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_SEARCH_H
#define MC_SEARCH_H

#include <stddef.h>
#include "mc_download.h"

#define MC_SEARCH_MAX_RESULTS 100

/* Search result struct — holds project metadata plus a direct download URL.
 * Callers must free fields with mc_search_result_free(). */
typedef struct {
    char *id;          /* project / addon id */
    char *slug;        /* project slug */
    char *name;        /* display name */
    char *description;
    char *logo_url;
    long long size;    /* primary file size in bytes (0 when unavailable) */
    char *download_url;/* direct download URL for the primary file */
    char *game_versions;
    char *loaders;
    char *project_type;/* "datapack" / "shader" / "resourcepack" */
    char *source;      /* "Modrinth" / "CurseForge" */
    int download_count;
} McSearchResult;

typedef enum {
    MC_SEARCH_SOURCE_MODRINTH = 1,
    MC_SEARCH_SOURCE_CURSEFORGE = 2,
    MC_SEARCH_SOURCE_ANY = 3,
} McSearchSource;

typedef enum {
    MC_SEARCH_SORT_RELEVANCE = 0,
    MC_SEARCH_SORT_DOWNLOADS = 1,
    MC_SEARCH_SORT_NEWEST = 2,
    MC_SEARCH_SORT_UPDATED = 3,
} McSearchSort;

typedef enum {
    MC_SEARCH_TYPE_DATAPACK = 0,
    MC_SEARCH_TYPE_SHADER,
    MC_SEARCH_TYPE_RESOURCEPACK,
} McSearchType;

void mc_search_result_init(McSearchResult *r);
void mc_search_result_free(McSearchResult *r);
void mc_search_results_free(McSearchResult *results, int count);

/* Configure the CurseForge API key. When unset only Modrinth is queried;
 * when set (or when the mcimirror relay is active) both sources are merged. */
void mc_search_set_cf_api_key(const char *api_key);
void mc_search_set_mirror(const char *mirror);

/* Search for datapacks, shaders, or resource packs across Modrinth and
 * optionally CurseForge. `type` selects the domain.
 * Returns the number of results written to `results` (max `max_results`). */
int mc_search(const char *query, const char *mc_version,
              const char *loader, int type,
              int source, int limit, int offset, int sort,
              McSearchResult *results, int max_results);

#endif
