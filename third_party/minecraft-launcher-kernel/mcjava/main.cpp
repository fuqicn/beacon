/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mcbase.h>
#include "private/include/mc_java.h"
#include <cstring>
#include <cstdio>

static void print_help(void) {
    mc_console_printf("mcjava - %s\n", mc_i18n("mcjava_desc"));
    mc_console_printf("%s: mcjava <%s> [%s]\n\n", mc_i18n("usage"), mc_i18n("commands"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("commands"));
    mc_console_printf("  list    %s\n", mc_i18n("list_cmd"));
    mc_console_printf("\n%s:\n", mc_i18n("options"));
    mc_console_printf("  --help, help  %s\n", mc_i18n("help_opt"));
    mc_console_printf("  --lang <code> %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json        %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug       %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  mcjava list\n");
    mc_console_printf("  mcjava list --lang zh\n");
}

int main(int argc, char **argv) {
    mc_console_init();
    mc_log_set_level(MC_LOG_WARN);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--lang") == 0) { mc_i18n_set(argv[i + 1]); break; }

    if (argc < 2) { print_help(); return 0; }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) { print_help(); return 0; }

    if (strcmp(argv[1], "list") == 0) {
        McJavaRuntime runtimes[MC_JAVA_MAX];
        int count = mc_java_find_all(runtimes, MC_JAVA_MAX);
        mc_console_printf("%s %d %s:\n", mc_i18n("found_versions"), count, mc_i18n("java_version"));
        for (int i = 0; i < count; i++) {
            McJavaRuntime *rt = &runtimes[i];
            mc_console_printf("  %d. %s %s (Java %d, %s, %s) %s\n",
                   i + 1,
                   rt->vendor,
                   rt->version,
                   rt->major_version,
                   rt->is_64bit ? "64-bit" : "32-bit",
                   rt->is_jdk ? "JDK" : "JRE",
                   rt->path);
        }
        return 0;
    }

    mc_console_printf("%s: %s\n", mc_i18n("unknown_command"), argv[1]);
    print_help();
    return 1;
}
