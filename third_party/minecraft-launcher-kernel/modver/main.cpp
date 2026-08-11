/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mc_mod.h>
#include <mc_i18n.h>
#include <mc_log.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

static void print_help(void) {
    mc_console_printf("modver - %s\n", mc_i18n("modver_desc"));
    mc_console_printf("%s: modver <project-id|slug|url> [%s]\n\n", mc_i18n("usage"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("options"));
    mc_console_printf("  --mcver <version>                 %s\n", mc_i18n("mod_mcver_filter"));
    mc_console_printf("  --loader <loader>                 %s\n", mc_i18n("mod_loader_filter"));
    mc_console_printf("  --mirror <type>                   %s\n", mc_i18n("mirror"));
    mc_console_printf("  --lang <code>                     %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json                            %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug                           %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  modver sodium\n");
    mc_console_printf("  modver sodium --mcver 1.20.1 --loader fabric\n");
    mc_console_printf("  modver https://modrinth.com/mod/sodium\n");
}

// extract project slug from Modrinth URL
static const char *extract_id(const char *input) {
    const char *p = strstr(input, "modrinth.com/");
    if (p) {
        p = strchr(p + 14, '/');
        if (p) return p + 1;
    }
    return input;
}

int main(int argc, char **argv) {
    mc_console_init();
    mc_log_set_level(MC_LOG_INFO);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    if (argc < 2) { print_help(); return 0; }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) { print_help(); return 0; }

    const char *input = nullptr;
    const char *mc_version = nullptr;
    const char *loader = nullptr;
    const char *mirror = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            mc_i18n_set(argv[++i]);
        } else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) {
            mirror = argv[++i];
        } else if (strcmp(argv[i], "--mcver") == 0 && i + 1 < argc) {
            mc_version = argv[++i];
        } else if (strcmp(argv[i], "--loader") == 0 && i + 1 < argc) {
            loader = argv[++i];
        } else if (argv[i][0] != '-') {
            input = argv[i];
        }
    }

    if (!input) { print_help(); return 1; }

    if (mirror) mc_mod_set_mirror(mirror);

    const char *project_id = extract_id(input);

    McModProject proj;
    mc_mod_project_init(&proj);
    int ok = mc_mod_get_project(project_id, MC_MOD_MODRINTH, &proj);

    if (!ok) {
        mc_error("%s: %s", mc_i18n("mod_not_found"), input);
        return 1;
    }

    mc_console_printf("%s: %s\n", mc_i18n("mod_name_label"), proj.name ? proj.name : "?");
    mc_console_printf("%s: %s\n", mc_i18n("mod_id"), proj.id ? proj.id : "?");
    mc_console_printf("%s: %s\n", mc_i18n("mod_slug"), proj.slug ? proj.slug : "?");
    mc_console_printf("Source: Modrinth\n");
    mc_console_printf("%s: %ld\n", mc_i18n("mod_downloads"), proj.download_count);
    if (proj.game_versions)
        mc_console_printf("%s: %s\n", mc_i18n("mod_versions"), proj.game_versions);
    if (proj.loaders)
        mc_console_printf("%s: %s\n", mc_i18n("mod_loaders"), proj.loaders);
    if (proj.description)
        mc_console_printf("%s: %s\n", mc_i18n("mod_desc"), proj.description);
    if (proj.website_url)
        mc_console_printf("%s: %s\n", mc_i18n("mod_website"), proj.website_url);

    mc_console_printf("\n--- %s ---\n", mc_i18n("mod_versions_list"));

    McModFile files[200];
    int fcount = mc_mod_get_versions(proj.id, MC_MOD_MODRINTH, mc_version, loader, files, 200);

    if (fcount == 0) {
        mc_console_printf("%s\n", mc_i18n("mod_no_versions"));
    } else {
        const char *h_version = mc_i18n("mod_ver_name");
        const char *h_type = mc_i18n("mod_ver_type");
        const char *h_date = mc_i18n("mod_ver_date");
        const char *h_mcver = mc_i18n("mod_versions");
        const char *h_loader_label = mc_i18n("mod_loaders");
        const char *h_size = mc_i18n("mod_ver_size");

        // filter headers
        mc_console_printf("%-40s %-10s %-22s %-8s %s\n",
                          h_version, h_type, h_date, h_size, "URL");
        mc_console_printf("%-40s %-10s %-22s %-8s %s\n",
                          "----", "----", "----", "----", "------------------");

        for (int i = 0; i < fcount && i < 40; i++) {
            McModFile *f = &files[i];
            std::string vname = f->display_name ? f->display_name : (f->file_name ? f->file_name : "?");
            if (vname.length() > 39) vname = vname.substr(0, 36) + "...";

            char size_str[32] = "";
            if (f->size > 0) snprintf(size_str, sizeof(size_str), "%ldKB", f->size / 1024);

            mc_console_printf("%-40s %-10s %-22s %-8s %s\n",
                              vname.c_str(),
                              f->release_type ? f->release_type : "?",
                              f->release_date ? f->release_date : "",
                              size_str,
                              f->download_url ? f->download_url : "");

            bool has_extra = (f->game_versions && f->game_versions[0]) ||
                             (f->loaders && f->loaders[0]);
            if (has_extra) {
                mc_console_printf("  ");
                if (f->game_versions && f->game_versions[0])
                    mc_console_printf("%s: %s", h_mcver, f->game_versions);
                if (f->loaders && f->loaders[0]) {
                    if (f->game_versions && f->game_versions[0])
                        mc_console_printf(" | ");
                    mc_console_printf("%s: %s", h_loader_label, f->loaders);
                }
                mc_console_printf("\n");
            }

            if (f->dependency_count > 0) {
                mc_console_printf("  Dependencies:");
                for (int j = 0; j < f->dependency_count; j++) {
                    McModDependency *d = &f->dependencies[j];
                    const char *type = d->dependency_type ? d->dependency_type : "unknown";
                    const char *pid = d->project_id ? d->project_id : "?";
                    mc_console_printf(" %s[%s]", pid, type);
                }
                mc_console_printf("\n");
            }
        }

        if (fcount > 40)
            mc_console_printf("\n(%s: %d)\n", mc_i18n("mod_more_files"), (fcount - 40));
    }

    mc_mod_project_free(&proj);
    for (int i = 0; i < fcount; i++)
        mc_mod_file_free(&files[i]);

    return 0;
}
