/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * GTK launcher for the Linux AppImage build. It is the AppRun of the launcher
 * AppImage (Beacon.AppImage) and replicates the Windows launcher mechanism:
 * on first run (or when the version changes) it installs
 * {beacon-app.AppImage, mirrors.json, version.txt} into <launcher dir>/beacon,
 * then launches the payload AppImage with APPIMAGE_EXTRACT_AND_RUN=1 and shows
 * a progress dialog until the "Beacon" window appears.
 *
 * Compile: gcc -O2 -s beacon_gtk.c -o BeaconLauncher $(pkg-config --cflags --libs gtk+-3.0 gdk-x11-3.0 x11)
 */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <errno.h>

#define BEACON_VERSION "1.0.0"
#define BEACON_DIR "beacon"
#define PAYLOAD_NAME "beacon-app.AppImage"
#define VERSION_FILE "version.txt"
#define MIRRORS_FILE "mirrors.json"
#define LOG_NAME "beacon_install.log"
#define WINDOW_TITLE "Beacon"

/* ---- Locale-aware strings ---- */
static int g_zh = 0;
static const char *L_TITLE;
static const char *L_READY;
static const char *L_UPDATING;
static const char *L_INIT;
static const char *L_ERR_NOTFOUND;
static const char *L_ERR_COPY;
static const char *L_ERR_LAUNCH;

static void init_lang(void) {
    const char *lang = getenv("LANG");
    if (lang && strstr(lang, "zh"))
        g_zh = 1;
    if (g_zh) {
        L_TITLE         = "Beacon Launcher";
        L_READY         = "正在准备...";
        L_UPDATING      = "正在更新 Beacon，请稍候...";
        L_INIT          = "正在初始化 Beacon，请稍候...";
        L_ERR_NOTFOUND  = "beacon-app.AppImage 未找到，安装可能不完整。";
        L_ERR_COPY      = "安装 beacon-app.AppImage 失败，请检查磁盘空间后重试。";
        L_ERR_LAUNCH    = "启动 Beacon 失败。";
    } else {
        L_TITLE         = "Beacon Launcher";
        L_READY         = "Preparing...";
        L_UPDATING      = "Updating Beacon, please wait...";
        L_INIT          = "Initializing Beacon, please wait...";
        L_ERR_NOTFOUND  = "beacon-app.AppImage not found. The installation may be incomplete.";
        L_ERR_COPY      = "Failed to install beacon-app.AppImage. Check disk space and retry.";
        L_ERR_LAUNCH    = "Failed to start Beacon.";
    }
}

static GtkWidget *g_window = NULL;
static GtkWidget *g_progress = NULL;
static GtkWidget *g_label = NULL;
static int g_progress_pos = 0;
static pid_t g_child = -1;
static int g_timeout_ms = 0;
static int g_x_available = 0;

/* Phase drives a single 0..100 sweep (copy caps at 90, launch 90->98,
   window-found jumps to 100). It never resets, so it never loops. */
enum { PHASE_COPY = 0, PHASE_LAUNCH = 1, PHASE_DONE = 2 };
static int g_phase = PHASE_COPY;

/* Carried into the install-and-launch idle callback. */
static char g_install_dir[4096];
static char g_payload[4096];
static char g_payload_dir[4096];
static char g_payload_ver_path[4096];
static int g_needs_update = 0;
static char g_expected[64] = "";
static int g_argc = 0;
static char **g_argv = NULL;

static void log_msg(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    const char *launcher_dir = getenv("BEACON_LAUNCHER_DIR");
    char path[4096];
    if (launcher_dir && launcher_dir[0])
        snprintf(path, sizeof(path), "%s/%s", launcher_dir, LOG_NAME);
    else
        snprintf(path, sizeof(path), "%s", LOG_NAME);

    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

static void set_progress(int pos) {
    g_progress_pos = pos;
    if (g_progress)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_progress), pos / 100.0);
}

static void set_label(const char *text) {
    if (g_label)
        gtk_label_set_text(GTK_LABEL(g_label), text);
}

static gboolean quit_main_after_short_delay(gpointer data) {
    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static gboolean progress_tick(gpointer data) {
    (void)data;
    if (!g_progress) return G_SOURCE_CONTINUE;
    if (g_phase == PHASE_COPY) {
        if (g_progress_pos < 90) set_progress(g_progress_pos + 1);
    } else if (g_phase == PHASE_LAUNCH) {
        if (g_progress_pos < 98) set_progress(g_progress_pos + 1);
    }
    return G_SOURCE_CONTINUE;
}

static void error_dialog(const char *text) {
    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", text);
    gtk_window_set_title(GTK_WINDOW(dlg), L_TITLE);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Read first line of a text file. Returns 1 on success, 0 if missing. */
static int read_text_file(const char *path, char *buf, int cap) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, cap, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        buf[n - 1] = '\0';
        n--;
    }
    return 1;
}

/* Copy a file, updating the copy-phase progress 0..90 based on bytes. Pumps
   pending GTK events between chunks so the progress window repaints while the
   copy blocks the main thread. */
static int copy_file_progress(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) return 0;
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    char *buf = malloc(1024 * 1024);
    if (!buf) {
        fclose(in);
        fclose(out);
        return 0;
    }
    long long total = st.st_size ? st.st_size : 1;
    long long done = 0;
    size_t n;
    while ((n = fread(buf, 1, 1024 * 1024, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            free(buf);
            fclose(in);
            fclose(out);
            return 0;
        }
        done += (long long)n;
        set_progress((int)((90 * done) / total));
        if (g_window) {
            while (gtk_events_pending())
                gtk_main_iteration_do(FALSE);
        }
    }
    free(buf);
    fclose(in);
    if (fclose(out) != 0) return 0;
    chmod(dst, 0755);
    return 1;
}

/* Recursively search the X tree for a window whose name contains "Beacon". */
static int find_beacon_window(Display *dpy, Window win) {
    char *name = NULL;
    if (XFetchName(dpy, win, &name)) {
        if (name) {
            int found = (strstr(name, WINDOW_TITLE) != NULL);
            XFree(name);
            if (found) return 1;
        }
    }
    Window root, parent, *children = NULL;
    unsigned int n;
    if (XQueryTree(dpy, win, &root, &parent, &children, &n)) {
        unsigned int i;
        for (i = 0; i < n; i++) {
            if (find_beacon_window(dpy, children[i])) {
                if (children) XFree(children);
                return 1;
            }
        }
        if (children) XFree(children);
    }
    return 0;
}

static gboolean launch_poll(gpointer data) {
    (void)data;
    if (g_phase == PHASE_DONE) return G_SOURCE_REMOVE;
    if (g_phase != PHASE_LAUNCH) return G_SOURCE_CONTINUE;

    int found = 0;
    if (g_x_available) {
        Display *dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        if (dpy)
            found = find_beacon_window(dpy, DefaultRootWindow(dpy));
    }

    int child_alive = 1;
    if (g_child > 0) {
        int st = 0;
        pid_t r = waitpid(g_child, &st, WNOHANG);
        if (r == g_child) {
            child_alive = 0;
            log_msg("payload exited with status %d", st);
        }
    }

    if (found) {
        log_msg("Beacon window found, closing progress dialog");
        g_phase = PHASE_DONE;
        set_progress(100);
        g_timeout_add(300, quit_main_after_short_delay, NULL);
        return G_SOURCE_REMOVE;
    }

    if (!child_alive) {
        g_phase = PHASE_DONE;
        set_progress(100);
        g_timeout_add(300, quit_main_after_short_delay, NULL);
        return G_SOURCE_REMOVE;
    }

    g_timeout_ms += 200;
    if (g_timeout_ms > 120000) {
        log_msg("Timeout waiting for Beacon window");
        g_phase = PHASE_DONE;
        set_progress(100);
        g_timeout_add(300, quit_main_after_short_delay, NULL);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void build_ui(void) {
    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), L_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(g_window), 380, 110);
    gtk_window_set_resizable(GTK_WINDOW(g_window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(g_window), 16);
    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_add(GTK_CONTAINER(g_window), vbox);

    g_label = gtk_label_new(L_READY);
    gtk_widget_set_halign(g_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), g_label, FALSE, FALSE, 0);

    g_progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_progress), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), g_progress, FALSE, FALSE, 0);

    gtk_widget_show_all(g_window);

    g_timeout_add(30, progress_tick, NULL);
}

/* Prepend a dir to an environment variable (colon separated), like a shell
   prepend_path. */
static void prepend_path(const char *var, const char *dir) {
    const char *cur = getenv(var);
    size_t len = strlen(dir) + (cur ? strlen(cur) : 0) + 2;
    char *buf = malloc(len);
    if (!buf) return;
    if (cur && cur[0])
        snprintf(buf, len, "%s:%s", dir, cur);
    else
        snprintf(buf, len, "%s", dir);
    setenv(var, buf, 1);
    free(buf);
}

/* Runs as an idle source once the GTK main loop is up, so the progress window
   stays responsive while the payload is being copied. */
static gboolean install_and_launch(gpointer data) {
    (void)data;

    if (g_needs_update) {
        if (g_window) {
            set_label(L_UPDATING);
            g_phase = PHASE_COPY;
            set_progress(0);
        }
        if (mkdir(g_install_dir, 0755) != 0 && errno != EEXIST) {
            log_msg("ERROR: cannot create install dir %s: %s",
                    g_install_dir, strerror(errno));
            if (g_window) error_dialog(L_ERR_COPY);
            return G_SOURCE_REMOVE;
        }
        char dst[4096];
        snprintf(dst, sizeof(dst), "%s/%s", g_install_dir, PAYLOAD_NAME);
        log_msg("Installing %s -> %s", g_payload, dst);
        if (!copy_file_progress(g_payload, dst)) {
            log_msg("ERROR: failed to copy payload");
            if (g_window) error_dialog(L_ERR_COPY);
            return G_SOURCE_REMOVE;
        }
        if (file_exists(g_payload_ver_path)) {
            char vdst[4096];
            snprintf(vdst, sizeof(vdst), "%s/%s", g_install_dir, VERSION_FILE);
            FILE *f = fopen(vdst, "w");
            if (f) {
                fprintf(f, "%s\n", g_expected);
                fclose(f);
            }
        }
        char mirror_src[4096], mirror_dst[4096];
        snprintf(mirror_src, sizeof(mirror_src), "%s/%s", g_payload_dir, MIRRORS_FILE);
        snprintf(mirror_dst, sizeof(mirror_dst), "%s/%s", g_install_dir, MIRRORS_FILE);
        if (file_exists(mirror_src)) {
            FILE *in = fopen(mirror_src, "rb");
            if (in) {
                FILE *out = fopen(mirror_dst, "wb");
                if (out) {
                    char buf[8192];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
                        fwrite(buf, 1, n, out);
                    fclose(out);
                }
                fclose(in);
            }
        }
        log_msg("Install complete");
    }

    char exe[4096];
    snprintf(exe, sizeof(exe), "%s/%s", g_install_dir, PAYLOAD_NAME);
    if (!file_exists(exe)) {
        log_msg("ERROR: %s not found", exe);
        if (g_window) error_dialog(L_ERR_NOTFOUND);
        return G_SOURCE_REMOVE;
    }

    if (g_window) {
        set_label(L_INIT);
        g_phase = PHASE_LAUNCH;
        set_progress(90);
    }

    log_msg("Launching: %s", exe);
    setenv("APPIMAGE_EXTRACT_AND_RUN", "1", 1);

    g_child = fork();
    if (g_child == 0) {
        char **args = malloc((size_t)(g_argc + 1) * sizeof(char *));
        if (!args) _exit(127);
        args[0] = exe;
        for (int i = 1; i < g_argc; i++) args[i] = g_argv[i];
        args[g_argc] = NULL;
        execv(exe, args);
        perror("execv");
        _exit(127);
    }
    if (g_child < 0) {
        log_msg("ERROR: fork failed: %s", strerror(errno));
        if (g_window) error_dialog(L_ERR_LAUNCH);
        return G_SOURCE_REMOVE;
    }

    if (g_window)
        g_timeout_add(200, launch_poll, NULL);
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    init_lang();

    /* Resolve the launcher directory (where the AppImage file lives) first so
       the log lands next to the launcher from the very first message. */
    const char *appimage = getenv("APPIMAGE");
    char launcher_dir[4096];
    if (appimage && appimage[0]) {
        snprintf(launcher_dir, sizeof(launcher_dir), "%s", appimage);
        char *slash = strrchr(launcher_dir, '/');
        if (slash) *slash = '\0';
    } else {
        snprintf(launcher_dir, sizeof(launcher_dir), "%s", argv[0]);
        char *slash = strrchr(launcher_dir, '/');
        if (slash) *slash = '\0';
        else snprintf(launcher_dir, sizeof(launcher_dir), ".");
    }
    setenv("BEACON_LAUNCHER_DIR", launcher_dir, 1);

    /* When running as an AppImage AppRun, this binary's GTK deps live in the
       mount under usr/lib; make them resolvable before initializing GTK. */
    {
        const char *apdir = getenv("APPDIR");
        if (apdir && apdir[0]) {
            char libdir[4096];
            char gi_dir[4096];
            snprintf(libdir, sizeof(libdir), "%s/usr/lib", apdir);
            if (file_exists(libdir)) {
                prepend_path("LD_LIBRARY_PATH", libdir);
                snprintf(gi_dir, sizeof(gi_dir), "%s/girepository-1.0", libdir);
                prepend_path("GI_TYPELIB_PATH", gi_dir);
            }
            snprintf(libdir, sizeof(libdir), "%s/usr/lib64", apdir);
            if (file_exists(libdir)) prepend_path("LD_LIBRARY_PATH", libdir);
        }
    }

    log_msg("BeaconLauncher (GTK) started, version %s", BEACON_VERSION);

    int rc = gtk_init_check(&argc, &argv);
    g_x_available = rc && GDK_IS_X11_DISPLAY(gdk_display_get_default());
    log_msg("GTK init: %s (X11: %d)", rc ? "ok" : "failed", g_x_available);
    if (rc)
        build_ui();

    char install_dir[4096];
    snprintf(install_dir, sizeof(install_dir), "%s/%s", launcher_dir, BEACON_DIR);
    log_msg("Install dir: %s", install_dir);

    /* Payload ships inside this AppImage's APPDIR. */
    const char *appdir = getenv("APPDIR");
    char payload_dir[4096];
    if (appdir && appdir[0]) {
        snprintf(payload_dir, sizeof(payload_dir), "%s", appdir);
    } else {
        snprintf(payload_dir, sizeof(payload_dir), "%s", launcher_dir);
    }
    char payload[4096];
    snprintf(payload, sizeof(payload), "%s/%s", payload_dir, PAYLOAD_NAME);
    log_msg("Payload: %s", payload);

    if (!file_exists(payload)) {
        log_msg("ERROR: payload not found: %s", payload);
        if (rc) error_dialog(L_ERR_NOTFOUND);
        else fprintf(stderr, "%s\n", L_ERR_NOTFOUND);
        return 1;
    }

    char payload_ver_path[4096];
    snprintf(payload_ver_path, sizeof(payload_ver_path), "%s/%s", payload_dir, VERSION_FILE);
    char installed_ver_path[4096];
    snprintf(installed_ver_path, sizeof(installed_ver_path), "%s/%s", install_dir, VERSION_FILE);

    char expected[64] = "", current[64] = "";
    read_text_file(payload_ver_path, expected, sizeof(expected));
    read_text_file(installed_ver_path, current, sizeof(current));
    int needs_update = 0;
    if (!file_exists(install_dir))
        needs_update = 1;
    else if (!file_exists(payload_ver_path) || !file_exists(installed_ver_path))
        needs_update = 1;
    else if (strcmp(current, expected) != 0)
        needs_update = 1;
    log_msg("Installed version: '%s', expected: '%s', needs update: %d",
            current, expected, needs_update);

    /* Hand the paths and decisions to the idle callback. */
    snprintf(g_install_dir, sizeof(g_install_dir), "%s", install_dir);
    snprintf(g_payload, sizeof(g_payload), "%s", payload);
    snprintf(g_payload_dir, sizeof(g_payload_dir), "%s", payload_dir);
    snprintf(g_payload_ver_path, sizeof(g_payload_ver_path), "%s", payload_ver_path);
    snprintf(g_expected, sizeof(g_expected), "%s", expected);
    g_needs_update = needs_update;
    g_argc = argc;
    g_argv = argv;

    if (g_window) {
        g_idle_add(install_and_launch, NULL);
        gtk_main();
    } else {
        /* No display: run the install/launch synchronously, then wait for the
           payload to finish. */
        install_and_launch(NULL);
        if (g_child > 0) {
            int waited = 0;
            for (;;) {
                int st = 0;
                pid_t r = waitpid(g_child, &st, WNOHANG);
                if (r == g_child) break;
                usleep(200000);
                waited += 200;
                if (waited >= 120000) break;
            }
        }
    }

    if (g_window) gtk_widget_destroy(g_window);
    if (g_child > 0) waitpid(g_child, NULL, 0);
    log_msg("Beacon launched successfully");
    return 0;
}