/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mclauncher.h"
#include <stdio.h>
int main() {
    char lang[8];
    mc_config_language_get(lang, sizeof(lang));
    printf("lang=%s\n", lang);
    mc_i18n_set(lang);
    printf("hello: %s\n", mc_i18n("version"));
    printf("error: %s\n", mc_i18n("unknown_command"));
    return 0;
}
