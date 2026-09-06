/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mclauncher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    mc_log_set_level(MC_LOG_DEBUG);
    const char *ver_id = argc > 1 ? argv[1] : "1.20.4";
    printf("Fetching version: %s\n", ver_id);
    
    // First get manifest
    McManifest *m = (McManifest *)malloc(sizeof(McManifest));
    memset(m, 0, sizeof(McManifest));
    printf("Fetching manifest...\n");
    if (!mc_manifest_fetch(m, 0)) {
        printf("Failed to fetch manifest\n");
        free(m);
        return 1;
    }
    printf("Manifest has %d versions\n", m->count);
    
    // Find version
    McVersionEntry entry;
    if (!mc_manifest_find(m, ver_id, &entry)) {
        printf("Version %s not found\n", ver_id);
        free(m);
        return 1;
    }
    printf("Version URL: %s\n", entry.url);
    
    // Now fetch the version JSON
    McVersion *v = (McVersion *)malloc(sizeof(McVersion));
    mc_version_init(v);
    printf("Fetching version JSON...\n");
    if (!mc_version_fetch(v, entry.url)) {
        printf("Failed to fetch version JSON\n");
        free(m);
        free(v);
        return 1;
    }
    printf("Success!\n");
    printf("ID: %s\n", v->id);
    printf("Main class: %s\n", v->main_class);
    printf("Libraries: %d\n", v->library_count);
    printf("Client size: %ld\n", v->client_size);
    
    mc_version_free(v);
    free(v);
    free(m);
    return 0;
}
