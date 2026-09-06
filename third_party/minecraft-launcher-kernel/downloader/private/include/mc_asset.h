/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_ASSET_H
#define MC_ASSET_H

#include "mc_version.h"

typedef struct {
    char hash[64];
    long size;
    char virtual_path[512];
} McAssetObject;

typedef struct {
    McAssetObject *objects;
    int count;
    int is_virtual;
    int map_to_resources;
} McAssetIndex;

int mc_asset_index_parse(McAssetIndex *idx, const char *json_data);
int mc_asset_index_fetch(McAssetIndex *idx, McVersion *v, const char *mc_dir);
void mc_asset_index_free(McAssetIndex *idx);
int mc_asset_url(const char *hash, char *out, size_t out_size);
int mc_asset_object_path(const char *hash, const char *mc_dir, char *out, size_t out_size);
int mc_asset_virtual_path(const char *hash, const char *mc_dir, const char *virtual_path, int map_to_resources, char *out, size_t out_size);

#endif
