/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mc_manifest.h>
#include <mc_i18n.h>
#include <mc_path.h>
#include <mc_log.h>
#include <cstring>
#include <cstdlib>

static void print_help() {
    mc_console_printf("mcsearch - %s\n", mc_i18n("mcsearch_desc"));
    mc_console_printf("%s: mcsearch [%s]\n\n", mc_i18n("usage"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("options"));
    mc_console_printf("  --type <release|snapshot|old_beta|old_alpha|all>  %s\n", mc_i18n("filter_type"));
    mc_console_printf("  --mirror <mojang|bmclapi|mcbbs>  %s\n", mc_i18n("mirror_source"));
    mc_console_printf("  --lang <code>  %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json         %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug        %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  mcsearch\n");
    mc_console_printf("  mcsearch --type release\n");
    mc_console_printf("  mcsearch --type all --mirror bmclapi\n");
}

int main(int argc, char **argv) {
    mc_console_init();
    mc_log_set_level(MC_LOG_ERROR);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    const char *mirror = "mojang";
    const char *filter_type = "all";

    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--lang") == 0) { mc_i18n_set(argv[i + 1]); break; }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            filter_type = argv[++i];
        } else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) {
            mirror = argv[++i];
        }
    }

    McManifest m;
    if (!mc_manifest_load_cache(&m)) {
        if (!mc_manifest_fetch_mirror(&m, 1, mirror)) {
            mc_error("%s", mc_i18n("err_fetch_manifest"));
            return 1;
        }
        mc_manifest_save_cache(&m);
    }

    for (int i = 0; i < m.count; i++) {
        bool match = false;
        if (strcmp(filter_type, "all") == 0) {
            match = true;
        } else if (strcmp(m.entries[i].type, filter_type) == 0) {
            match = true;
        }
        if (match) {
            char release_time[64] = "", modified_time[64] = "";
            if (m.entries[i].release_time > 0) {
                time_t rt = static_cast<time_t>(m.entries[i].release_time);
                struct tm *tm = gmtime(&rt);
                if (tm) strftime(release_time, sizeof(release_time), "%Y-%m-%dT%H:%M:%SZ", tm);
            }
            if (m.entries[i].modified_time > 0) {
                time_t mt = static_cast<time_t>(m.entries[i].modified_time);
                struct tm *tm = gmtime(&mt);
                if (tm) strftime(modified_time, sizeof(modified_time), "%Y-%m-%dT%H:%M:%SZ", tm);
            }
            mc_console_printf("%s|%s|%s|%s\n", m.entries[i].type, m.entries[i].id,
                              release_time, modified_time);
        }
    }

    return 0;
}
