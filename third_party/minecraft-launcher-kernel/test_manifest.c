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
    McManifest *m = (McManifest *)malloc(sizeof(McManifest));
    memset(m, 0, sizeof(McManifest));
    printf("sizeof McManifest: %llu\n", (unsigned long long)sizeof(McManifest));
    printf("Fetching manifest...\n");
    int r = mc_manifest_fetch(m, 0);
    printf("Result: %d, count: %d\n", r, m->count);
    printf("Latest: %s\n", m->latest_release);
    if (r && m->count > 0)
        printf("First: %s (%s)\n", m->entries[0].id, m->entries[0].type);
    free(m);
    return 0;
}
