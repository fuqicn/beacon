/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mcbase.h>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cstdio>

static void print_help(void) {
    mc_console_printf("mcver - %s\n", mc_i18n("version"));
    mc_console_printf("%s: mcver <%s> [%s]\n\n", mc_i18n("usage"), mc_i18n("commands"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("commands"));
    mc_console_printf("  list [--type <release|snapshot|old|all>]   %s\n", mc_i18n("info"));
    mc_console_printf("  search <query>                              %s\n", mc_i18n("search_results"));
    mc_console_printf("  info <version>                              %s\n", mc_i18n("info"));
    mc_console_printf("  manifest [--refresh]                        %s\n", mc_i18n("refresh"));
    mc_console_printf("  latest                                      %s %s\n", mc_i18n("latest_release"), mc_i18n("info"));
    mc_console_printf("  help                                        %s\n", mc_i18n("help"));
    mc_console_printf("\n%s:\n", mc_i18n("options"));
    mc_console_printf("  --mirror <type>   %s: mojang, bmclapi, mcbbs\n", mc_i18n("mirror"));
    mc_console_printf("  --lang <code>     %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json            %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug           %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  mcver list\n");
    mc_console_printf("  mcver list --type release\n");
    mc_console_printf("  mcver search 1.20\n");
    mc_console_printf("  mcver info 1.20.4\n");
    mc_console_printf("  mcver manifest --refresh\n");
    mc_console_printf("  mcver latest\n");
}

static const char *g_mirror = nullptr;

static McManifest *fetch_manifest(int force_refresh) {
    McManifest *m = (McManifest *)malloc(sizeof(McManifest));
    if (!m) return nullptr;
    memset(m, 0, sizeof(McManifest));
    if (!mc_manifest_fetch_mirror(m, force_refresh, g_mirror)) {
        free(m);
        return nullptr;
    }
    return m;
}

static void cmd_list(int argc, char **argv) {
    const char *filter = "all";
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc)
            filter = argv[++i];
    }
    McManifest *m = fetch_manifest(0);
    if (!m) {
        mc_error("%s: %s", mc_i18n("error"), mc_i18n("no_matching"));
        return;
    }
    mc_console_printf("%-24s %-12s %s\n", "ID", mc_i18n("type"), mc_i18n("release_time"));
    mc_console_printf("%-24s %-12s %s\n", "----", "----", "------------");
    for (int i = 0; i < m->count; i++) {
        if (strcmp(filter, "all") == 0 || strcmp(m->entries[i].type, filter) == 0) {
            time_t t = (time_t)m->entries[i].release_time;
            struct tm *tm = localtime(&t);
            char timebuf[32];
            if (tm) strftime(timebuf, sizeof(timebuf), "%Y-%m-%d", tm);
            else snprintf(timebuf, sizeof(timebuf), "%s", "unknown");
            mc_console_printf("%-24s %-12s %s\n", m->entries[i].id, m->entries[i].type, timebuf);
        }
    }
    mc_console_printf("\n%s: %d\n", mc_i18n("total_versions"), m->count);
    mc_console_printf("%s: %s\n", mc_i18n("latest_release"), m->latest_release);
    mc_console_printf("%s: %s\n", mc_i18n("latest_snapshot"), m->latest_snapshot);
    free(m);
}

static void cmd_search(int argc, char **argv) {
    if (argc < 3) { mc_console_printf("%s: mcver search <query>\n", mc_i18n("usage")); return; }
    const char *query = argv[2];
    McManifest *m = fetch_manifest(0);
    if (!m) { mc_error("%s: %s", mc_i18n("error"), mc_i18n("no_matching")); return; }
    McVersionEntry results[100];
    int found = mc_manifest_search(m, query, results, 100);
    if (found == 0) { mc_error("%s '%s'", mc_i18n("no_matching"), query); free(m); return; }
    mc_console_printf("%s '%s': %d\n\n", mc_i18n("found_versions"), query, found);
    mc_console_printf("%-24s %-12s %s\n", "ID", mc_i18n("type"), mc_i18n("release_time"));
    mc_console_printf("%-24s %-12s %s\n", "----", "----", "------------");
    for (int i = 0; i < found; i++) {
        time_t t = (time_t)results[i].release_time;
        struct tm *tm = localtime(&t);
        char timebuf[32];
        if (tm) strftime(timebuf, sizeof(timebuf), "%Y-%m-%d", tm);
        else snprintf(timebuf, sizeof(timebuf), "%s", "unknown");
        mc_console_printf("%-24s %-12s %s\n", results[i].id, results[i].type, timebuf);
    }
    free(m);
}

static void cmd_info(int argc, char **argv) {
    if (argc < 3) { mc_console_printf("%s: mcver info <version>\n", mc_i18n("usage")); return; }
    McVersion *v = (McVersion *)malloc(sizeof(McVersion));
    if (!v) { mc_error("%s: %s", mc_i18n("error"), mc_i18n("failed_parse_version")); return; }
    mc_version_init(v);
    int ok;
    if (g_mirror) ok = mc_version_fetch_by_id_mirror(v, argv[2], g_mirror);
    else ok = mc_version_fetch_by_id(v, argv[2]);
    if (!ok) {
        mc_error("%s: %s '%s'", mc_i18n("error"), mc_i18n("failed_parse_version"), argv[2]);
        free(v);
        return;
    }
    mc_console_printf("%s: %s\n", mc_i18n("version"), v->id);
    mc_console_printf("  %s: %s\n", mc_i18n("type"), v->type);
    mc_console_printf("  %s: %s\n", mc_i18n("main_class"), v->main_class);
    mc_console_printf("  %s: %d\n", mc_i18n("java_version"), v->java_major_version);
    mc_console_printf("  %s: %s\n", mc_i18n("inherits_from"), v->inherits_from);
    mc_console_printf("  %s: %d\n", mc_i18n("libraries"), v->library_count);
    mc_console_printf("  %s: %ld %s\n", mc_i18n("client_size"), v->client_size, mc_i18n("bytes"));
    mc_console_printf("  %s: %s\n", mc_i18n("asset_index"), v->asset_index.id);
    if (v->assets[0]) mc_console_printf("  %s: %s\n", mc_i18n("assets"), v->assets);
    if (v->minecraft_arguments[0])
        mc_console_printf("  %s: %s\n", mc_i18n("arguments"), v->minecraft_arguments);
    mc_version_free(v);
    free(v);
}

static void cmd_manifest(int argc, char **argv) {
    int refresh = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--refresh") == 0) refresh = 1;
    }
    McManifest *m = fetch_manifest(refresh);
    if (!m) { mc_error("%s: %s", mc_i18n("error"), mc_i18n("no_matching")); return; }
    mc_console_printf("%s: %d %s\n", mc_i18n("manifest_updated"), m->count, mc_i18n("version"));
    mc_console_printf("Latest release: %s\n", m->latest_release);
    mc_console_printf("Latest snapshot: %s\n", m->latest_snapshot);
    free(m);
}

static void cmd_latest(void) {
    McManifest *m = fetch_manifest(0);
    if (!m) { mc_error("Error: Failed to fetch version manifest"); return; }
    mc_console_printf("%s\n", m->latest_release);
    free(m);
}

int main(int argc, char **argv) {
    mc_console_init();
    mc_log_set_level(MC_LOG_INFO);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    const char *lang = nullptr;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--mirror") == 0) {
            g_mirror = argv[i + 1];
            for (int j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
            argc -= 2;
            break;
        }
    }
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--lang") == 0) {
            lang = argv[i + 1];
            for (int j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
            argc -= 2;
            break;
        }
    }
    if (lang) mc_i18n_set(lang);

    if (argc < 2) { print_help(); return 0; }
    if (strcmp(argv[1], "list") == 0) { cmd_list(argc, argv); return 0; }
    if (strcmp(argv[1], "search") == 0) { cmd_search(argc, argv); return 0; }
    if (strcmp(argv[1], "info") == 0) { cmd_info(argc, argv); return 0; }
    if (strcmp(argv[1], "manifest") == 0) { cmd_manifest(argc, argv); return 0; }
    if (strcmp(argv[1], "latest") == 0) { cmd_latest(); return 0; }
    mc_error("%s: %s", mc_i18n("unknown_command"), argv[1]);
    print_help();
    return 1;
}
