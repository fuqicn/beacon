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

/* Search result — holds project metadata plus a best-effort download URL.
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
 * Returns the number of results written to `results` (max `max_results`).
 *
 * Note: Modrinth search results do not include file download URLs. The
 * search function fills download_url with a best-effort CDN path
 * (https://cdn.modrinth.com/data/{id}/versions/{latest_version}/). To get
 * accurate file metadata (exact URL, sha1, size) call mc_search_get_files(). */
int mc_search(const char *query, const char *mc_version,
              const char *loader, int type,
              int source, int limit, int offset, int sort,
              McSearchResult *results, int max_results);

/* ---------------------------------------------------------------------------
 * File lookup
 *
 * Modrinth search hits carry `latest_version` (a version ID string) but do
 * NOT include the file download artifact. Call this API to resolve the
 * actual download URL, sha1, and size for a specific project.
 *
 * If `mc_version` is non-empty, only versions matching that MC version are
 * returned. If `loader` is non-empty, only versions whose loaders list
 * contains the given string are returned.
 *
 * Returns the number of file entries written to `out` (max `max_files`).
 * Callers must free entries with mc_search_files_free(out, count).
 * --------------------------------------------------------------------------- */
typedef struct {
    char *version_id;    /* version ID string */
    char *file_id;       /* file artifact id  */
    char *file_name;     /* filename e.g. "Foo_v1.0.zip" */
    char *download_url;  /* direct download URL */
    char *sha1;          /* sha1 hash (may be empty) */
    long long size;      /* file size in bytes */
    char *version_type;  /* "release" / "beta" / "alpha" */
    char *date_published;
    int is_primary;      /* 1 if this is the primary file for this version */
} McSearchFile;

int mc_search_get_files(const char *project_id,
                        const char *mc_version, const char *loader,
                        McSearchFile *out, int max_files);

void mc_search_file_free(McSearchFile *f);
void mc_search_files_free(McSearchFile *files, int count);

#endif
