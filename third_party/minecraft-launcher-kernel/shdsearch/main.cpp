/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mc_search.h>
#include <mc_i18n.h>
#include <mc_log.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

static void print_help(void) {
    mc_console_printf("shdsearch - %s\n", mc_i18n("shdsearch_desc"));
    mc_console_printf("%s: shdsearch [query] [%s]\n\n", mc_i18n("usage"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("options"));
    mc_console_printf("  --cfapi <key>      %s\n", mc_i18n("cf_api_key_opt"));
    mc_console_printf("  --mc-ver <ver>     %s\n", mc_i18n("shd_mc_ver"));
    mc_console_printf("  --sort <downloads|relevance|newest|updated>  %s\n", mc_i18n("mod_sort"));
    mc_console_printf("  --limit <n>        %s (default: 20)\n", mc_i18n("mod_limit"));
    mc_console_printf("  --page <n>         %s (default: 0)\n", mc_i18n("mod_page"));
    mc_console_printf("  --mirror <type>    %s\n", mc_i18n("mirror"));
    mc_console_printf("  --lang <code>      %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json             %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug            %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  shdsearch\n");
    mc_console_printf("  shdsearch selenium\n");
    mc_console_printf("  shdsearch --mc-ver 1.20.1 --sort downloads\n");
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
    const char *mc_version = nullptr;
    const char *loader = nullptr;
    const char *mirror = nullptr;
    const char *cf_api_key = nullptr;
    int sort = MC_SEARCH_SORT_DOWNLOADS;
    int limit = 20;
    int page = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(); return 0;
        } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            mc_i18n_set(argv[++i]);
        } else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) {
            mirror = argv[++i];
        } else if (strcmp(argv[i], "--cfapi") == 0 && i + 1 < argc) {
            cf_api_key = argv[++i];
        } else if (strcmp(argv[i], "--mc-ver") == 0 && i + 1 < argc) {
            mc_version = argv[++i];
        } else if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            const char *val = argv[++i];
            if (strcmp(val, "relevance") == 0) sort = MC_SEARCH_SORT_RELEVANCE;
            else if (strcmp(val, "downloads") == 0) sort = MC_SEARCH_SORT_DOWNLOADS;
            else if (strcmp(val, "newest") == 0) sort = MC_SEARCH_SORT_NEWEST;
            else if (strcmp(val, "updated") == 0) sort = MC_SEARCH_SORT_UPDATED;
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

    if (cf_api_key) mc_search_set_cf_api_key(cf_api_key);
    if (mirror) mc_search_set_mirror(mirror);

    if (!query) sort = MC_SEARCH_SORT_DOWNLOADS;
    else if (sort == MC_SEARCH_SORT_DOWNLOADS) sort = MC_SEARCH_SORT_RELEVANCE;

    McSearchResult results[200];
    int offset = page * limit;
    int count = mc_search(query, mc_version, loader, MC_SEARCH_TYPE_SHADER,
                          MC_SEARCH_SOURCE_ANY, limit, offset, sort,
                          results, 200);

    if (count == 0) {
        if (query)
            mc_console_printf("%s '%s'\n", mc_i18n("shd_none_query"), query);
        else
            mc_console_printf("%s\n", mc_i18n("shd_none"));
        return 1;
    }

    mc_console_printf("%-28s %-48s %-12s %s\n",
                      mc_i18n("mod_id"), mc_i18n("mod_name"),
                      mc_i18n("mod_downloads"), mc_i18n("mod_versions"));
    mc_console_printf("%-28s %-48s %-12s %s\n",
                      "----", "----", "----", "----------------------");

    for (int i = 0; i < count; i++) {
        McSearchResult *r = &results[i];
        mc_console_printf("%-28s %-48s %-12ld %s [%s]\n",
                          r->id ? r->id : "",
                          r->name ? r->name : "",
                          r->download_count,
                          r->game_versions ? r->game_versions : "",
                          r->source ? r->source : "");

        // Resolve accurate download URL via the files API for Modrinth hits.
        if (r->source && strcmp(r->source, "Modrinth") == 0) {
            McSearchFile files[5];
            int fc = mc_search_get_files(r->id, mc_version, loader,
                                         files, 5);
            for (int j = 0; j < fc; j++) {
                if (files[j].download_url && files[j].download_url[0]) {
                    free(r->download_url);
                    r->download_url = strdup(files[j].download_url);
                    if (!r->size)
                        r->size = files[j].size;
                    break;
                }
            }
            mc_search_files_free(files, fc);
        }

        if (r->download_url && r->download_url[0])
            mc_console_printf("  DL: %s\n", r->download_url);
        if (r->description && r->description[0]) {
            std::string desc = r->description;
            if (desc.length() > 90) desc = desc.substr(0, 87) + "...";
            mc_console_printf("  %s\n", desc.c_str());
        }
    }

    mc_search_results_free(results, count);
    return 0;
}
