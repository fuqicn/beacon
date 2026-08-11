/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mcbase.h>
#include "private/include/mc_auth.h"
#include "private/include/mc_auth_msa.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include <QtCore/QFile>
#include <QtCore/QCoreApplication>

static void print_help(void) {
    mc_console_printf("login %s %s.\n", mc_i18n("usage"), mc_i18n("login_desc"));
    mc_console_printf("\n%s:\n", mc_i18n("commands"));
    mc_console_printf("  login        %s\n", mc_i18n("login_usage"));
    mc_console_printf("  refresh      %s\n", mc_i18n("refresh_usage"));
    mc_console_printf("  validate     %s\n", mc_i18n("validate_usage"));
    mc_console_printf("  logout       %s\n", mc_i18n("logout_usage"));
    mc_console_printf("\n%s:\n", mc_i18n("options"));
    mc_console_printf("  --session <path>  %s\n", mc_i18n("session_opt"));
    mc_console_printf("  --lang <code>     %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --help            %s\n", mc_i18n("help_opt"));
    mc_console_printf("  --json            %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug           %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  login login\n");
    mc_console_printf("  login login --session my_session.json\n");
    mc_console_printf("  login refresh --session my_session.json\n");
    mc_console_printf("  login logout --session my_session.json\n");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    mc_console_init();
    mc_log_set_level(MC_LOG_INFO);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    const char *session_path = nullptr;

    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--lang") == 0) { mc_i18n_set(argv[i + 1]); break; }

    if (argc < 2) { print_help(); return 0; }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) { print_help(); return 0; }

    int i;
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--session") == 0 && i + 1 < argc) session_path = argv[++i];
    }

    char default_session[MC_PATH_MAX] = "";
    if (!session_path) {
        char appdata[MC_PATH_MAX];
        if (mc_path_appdata(appdata, sizeof(appdata)))
            snprintf(default_session, sizeof(default_session), "%s/session.json", appdata);
        else
            snprintf(default_session, sizeof(default_session), "session.json");
        session_path = default_session;
    }

    if (strcmp(argv[1], "login") == 0) {
        McAuthSession session;
        mc_auth_init(&session);

        mc_info("Opening browser for Microsoft login...");
        if (!mc_auth_msa_login(&session)) {
            mc_error("Microsoft login failed: %s", session.error[0] ? session.error : "Unknown error");
            return 1;
        }

        mc_info("Logged in as: %s", session.name);
        mc_info("UUID: %s", session.uuid);

        if (!mc_auth_save(&session, session_path)) {
            mc_error("Failed to save session to %s", session_path);
            return 1;
        }
        mc_info("Session saved to %s", session_path);
        return 0;
    }

    if (strcmp(argv[1], "logout") == 0) {
        if (mc_path_exists(session_path)) {
            QFile::remove(QString::fromUtf8(session_path));
            mc_info("Session removed: %s", session_path);
        } else {
            mc_info("No session found at %s", session_path);
        }
        return 0;
    }

    if (strcmp(argv[1], "refresh") == 0) {
        if (!mc_path_exists(session_path)) {
            mc_error("No session file at %s", session_path);
            return 1;
        }
        McAuthSession session;
        mc_auth_init(&session);
        if (!mc_auth_load(&session, session_path)) {
            mc_error("Failed to load session from %s", session_path);
            return 1;
        }
        mc_info("Refreshing session for %s...", session.name);
        if (!mc_auth_msa_refresh(&session)) {
            mc_error("Refresh failed: %s", session.error[0] ? session.error : "Unknown error");
            return 1;
        }
        mc_info("Session refreshed for %s", session.name);
        if (!mc_auth_save(&session, session_path)) {
            mc_error("Failed to save refreshed session");
            return 1;
        }
        mc_info("Refreshed session saved to %s", session_path);
        return 0;
    }

    if (strcmp(argv[1], "validate") == 0) {
        if (!mc_path_exists(session_path)) {
            mc_error("No session file at %s", session_path);
            return 1;
        }
        McAuthSession session;
        mc_auth_init(&session);
        if (!mc_auth_load(&session, session_path)) {
            mc_error("Failed to load session from %s", session_path);
            return 1;
        }
        mc_info("Validating session for %s...", session.name);
        if (session.msa_refresh_token[0]) {
            if (mc_auth_msa_refresh(&session)) {
                mc_info("Session valid for %s", session.name);
                mc_auth_save(&session, session_path);
                return 0;
            }
            mc_error("Session expired and refresh failed: %s", session.error);
            return 1;
        }
        if (mc_auth_validate(&session)) {
            mc_info("Session valid for %s", session.name);
            return 0;
        }
        if (mc_auth_refresh(&session)) {
            mc_info("Session refreshed for %s", session.name);
            mc_auth_save(&session, session_path);
            return 0;
        }
        mc_error("Session expired and refresh failed");
        return 1;
    }

    mc_console_printf("%s: %s\n\n", mc_i18n("unknown_command"), argv[1]);
    print_help();
    return 1;
}
