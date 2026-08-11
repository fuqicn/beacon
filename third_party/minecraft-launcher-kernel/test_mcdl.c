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

int main() {
    mc_log_set_level(MC_LOG_DEBUG);
    printf("Starting...\n");
    
    McVersion *v = (McVersion *)malloc(sizeof(McVersion));
    printf("Allocated McVersion at %p\n", (void*)v);
    if (!v) { printf("OOM\n"); return 1; }
    
    mc_version_init(v);
    printf("Initialized\n");
    
    printf("Fetching by id...\n");
    int r = mc_version_fetch_by_id(v, "1.20.4");
    printf("Result: %d\n", r);
    printf("ID: %s\n", v->id);
    
    // Test saving to file
    printf("Testing download_version_json equivalent...\n");
    char versions_dir[MC_PATH_MAX];
    mc_path_join("test_mc", "versions", versions_dir, sizeof(versions_dir));
    mc_path_mkdir_p(versions_dir);
    char ver_dir[MC_PATH_MAX];
    mc_path_join(versions_dir, v->id, ver_dir, sizeof(ver_dir));
    mc_path_mkdir_p(ver_dir);
    char ver_json_path[1024];
    snprintf(ver_json_path, sizeof(ver_json_path), "%s/%s.json", ver_dir, v->id);
    printf("Writing to %s\n", ver_json_path);
    if (v->raw_json) {
        char *json_str = mc_json_stringify(v->raw_json);
        if (json_str) {
            FILE *f = fopen(ver_json_path, "w");
            if (f) { fputs(json_str, f); fclose(f); printf("Wrote JSON\n"); }
            free(json_str);
        }
    }
    
    mc_version_free(v);
    free(v);
    printf("Done\n");
    return 0;
}
