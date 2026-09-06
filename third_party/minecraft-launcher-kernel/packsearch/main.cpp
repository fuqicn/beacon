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
#include <mc_download.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

static void print_help(void) {
    mc_console_printf("packsearch - %s\n", mc_i18n("packsearch_desc"));
    mc_console_printf("%s: packsearch [query] [%s]\n\n", mc_i18n("usage"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("options"));
    mc_console_printf("  --sort <downloads|relevance|follows|newest>  %s\n", mc_i18n("mod_sort"));
    mc_console_printf("  --limit <n>  %s (default: 20)\n", mc_i18n("mod_limit"));
    mc_console_printf("  --page <n>  %s (default: 0)\n", mc_i18n("mod_page"));
    mc_console_printf("  --mirror <type>  %s\n", mc_i18n("mirror"));
    mc_console_printf("  --lang <code>  %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json          %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug         %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  packsearch\n");
    mc_console_printf("  packsearch skyfactory\n");
    mc_console_printf("  packsearch --sort downloads --page 1 --limit 30\n");
}

int main(int argc, char **argv) {
    mc_console_init();
    mc_log_set_level(MC_LOG_ERROR);

    if (mc_mirror_load_config("mirrors.json"))
        mc_info("Loaded mirror config from mirrors.json");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    const char *query = nullptr;
    const char *mirror = nullptr;
    int sort = MC_MOD_SORT_DOWNLOADS;
    int limit = 20;
    int page = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(); return 0;
        } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            mc_i18n_set(argv[++i]);
        } else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) {
            mirror = argv[++i];
        } else if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            const char *val = argv[++i];
            if (strcmp(val, "relevance") == 0) sort = MC_MOD_SORT_RELEVANCE;
            else if (strcmp(val, "downloads") == 0) sort = MC_MOD_SORT_DOWNLOADS;
            else if (strcmp(val, "follows") == 0) sort = MC_MOD_SORT_FOLLOWS;
            else if (strcmp(val, "newest") == 0) sort = MC_MOD_SORT_NEWEST;
            else if (strcmp(val, "updated") == 0) sort = MC_MOD_SORT_UPDATED;
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = atoi(argv[++i]);
            if (limit < 1) limit = 1;
            if (limit > 100) limit = 100;
        } else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
            page = atoi(argv[++i]);
            if (page < 0) page = 0;
        } else if (argv[i][0] != '-') {
            query = argv[i];
        }
    }

    if (!query) { sort = MC_MOD_SORT_DOWNLOADS; }
    else if (sort == MC_MOD_SORT_DOWNLOADS) { sort = MC_MOD_SORT_RELEVANCE; }

    if (mirror) mc_mod_set_mirror(mirror);

    McModProject results[200];
    int offset = page * limit;
    int count = mc_mod_search_pack(query, nullptr, nullptr,
                                   limit, offset, sort, results, 200);

    if (count == 0) {
        if (query)
            mc_console_printf("%s '%s'\n", mc_i18n("pack_none_query"), query);
        else
            mc_console_printf("%s\n", mc_i18n("pack_none"));
        return 1;
    }

    const char *heading_id = mc_i18n("mod_id");
    const char *heading_name = mc_i18n("mod_name");
    const char *heading_dl = mc_i18n("mod_downloads");
    const char *heading_ver = mc_i18n("mod_versions");

    mc_console_printf("%-28s %-48s %-12s %s\n",
                      heading_id, heading_name, heading_dl, heading_ver);
    mc_console_printf("%-28s %-48s %-12s %s\n",
                      "----", "----", "----", "----------------------");

    for (int i = 0; i < count; i++) {
        McModProject *p = &results[i];
        mc_console_printf("%-28s %-48s %-12ld %s\n",
                          p->id ? p->id : "",
                          p->name ? p->name : "",
                          p->download_count,
                          p->game_versions ? p->game_versions : "");

        if (p->description && p->description[0]) {
            std::string desc = p->description;
            if (desc.length() > 90) desc = desc.substr(0, 87) + "...";
            mc_console_printf("  %s\n", desc.c_str());
        }
    }

    for (int i = 0; i < count; i++)
        mc_mod_project_free(&results[i]);

    return 0;
}
