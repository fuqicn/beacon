/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_MANIFEST_H
#define MC_MANIFEST_H

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#define MC_VERSION_ID_MAX 64
#define MC_MANIFEST_MAX_VERSIONS 2048

typedef struct {
    char id[MC_VERSION_ID_MAX];
    char type[32];
    char url[512];
    char sha1[64];
    long long release_time;
    long long modified_time;
} McVersionEntry;

typedef struct {
    McVersionEntry entries[MC_MANIFEST_MAX_VERSIONS];
    int count;
    char latest_release[64];
    char latest_snapshot[64];
    long long fetch_time;
} McManifest;

int mc_manifest_fetch(McManifest *m, int force_refresh);
int mc_manifest_fetch_mirror(McManifest *m, int force_refresh, const char *mirror_type);
int mc_manifest_load_cache(McManifest *m);
int mc_manifest_save_cache(const McManifest *m);
int mc_manifest_find(const McManifest *m, const char *id, McVersionEntry *out);
int mc_manifest_search(const McManifest *m, const char *query, McVersionEntry *results, int max_results);

// Mirror URL helpers
const char *mc_manifest_url_for_mirror(const char *mirror_type);

#endif
