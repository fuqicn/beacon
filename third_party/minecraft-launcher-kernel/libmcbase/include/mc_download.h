/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_DOWNLOAD_H
#define MC_DOWNLOAD_H

#include <stddef.h>

#define MC_DL_MAX_MIRRORS 4

typedef struct {
    char url[2048];
    char output_path[1024];
    char expected_sha1[64];
    long expected_size;
    int retry_count;
    int max_retries;
} McDownloadTask;

typedef struct {
    int success;
    char error[512];
    long downloaded_bytes;
} McDownloadResult;

typedef struct {
    char mirrors[MC_DL_MAX_MIRRORS][2048];
    int mirror_count;
    int max_retries;
    int timeout_ms;
    int verify_hash;
} McDownloader;

void mc_downloader_init(McDownloader *dl);
void mc_downloader_add_mirror(McDownloader *dl, const char *url);
int mc_download_file(McDownloader *dl, McDownloadTask *task, McDownloadResult *result);
int mc_download_url(const char *url, const char *output_path, McDownloadResult *result);

// URL mirror translation helpers
int mc_download_translate_mojang_url(const char *url, char *mirror, size_t mirror_size, const char *mirror_type);

// Global download source selection ("mojang", "auto", "bmclapi", "mcimirror", "mcbbs", ...).
// Passing "" or NULL restores "auto".
void mc_download_set_mirror(const char *mirror_type);

// Currently configured mirror type (default "auto").
const char *mc_download_mirror(void);

// Resolved mirror type: if configured type is "auto", probes all mirrors once
// (cached for a few minutes) and returns the best available one. Never returns
// NULL; falls back to "mojang".
const char *mc_download_effective_mirror(void);

// Translate using the effective mirror (resolves "auto" at call time).
int mc_download_translate_mojang_url_auto(const char *url, char *mirror, size_t mirror_size);

// Mirror config (loaded from JSON, replaces hardcoded rules)
#define MC_MIRROR_MAX_RULES 16
#define MC_MIRROR_MAX_ENTRIES 8

typedef struct {
    char match[256];
    char prefix[256];
} McMirrorRule;

typedef struct {
    char name[64];
    char base_url[512];
    int rule_count;
    McMirrorRule rules[MC_MIRROR_MAX_RULES];
} McMirrorEntry;

// Load mirror config from a JSON file (merges into loaded configs).
int mc_mirror_load_config(const char *path);

// Load mirror config from a JSON string (merges into loaded configs). 
int mc_mirror_load_config_json(const char *json_data);

// Clear all loaded configs (reverts to hardcoded fallback).
void mc_mirror_clear_config(void);

// Get the number of loaded mirror entries.
int mc_mirror_config_count(void);

// Get a loaded mirror entry by index.
const McMirrorEntry *mc_mirror_config_get(int index);

// Mirror health probe
#define MC_MIRROR_PROBE_TIMEOUT_MS 5000
#define MC_MAX_MIRROR_TYPES 8

typedef struct {
    char mirror_type[64];
    double latency_ms;
    int available;
    int status_code;
} McMirrorProbe;

// Probe a specific mirror type. Returns 1 if probe was attempted (result may still show unavailable).
int mc_mirror_probe(const char *mirror_type, McMirrorProbe *result);

// Probe all known mirror types. Returns number of probed types.
int mc_mirror_probe_all(McMirrorProbe *results, int max_results);

// Find the best available mirror from probe results. Returns mirror_type string or "mojang".
const char *mc_mirror_select_best(McMirrorProbe *results, int count);

// Get the test URL used for mirror probing.
const char *mc_mirror_get_test_url(void);

#endif
