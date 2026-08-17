/*
 * Beacon self-extracting AppImage AppRun.
 * Pure C + GTK3, windows have no title bar (undecorated).
 *
 * Modes (selected via BEACON_UPDATER=1):
 *   RUN    (default): launch the installed app (exec <install>/usr/bin/Beacon)
 *                     with the runtime env linuxdeploy would have set.
 *   UPDATE (BEACON_UPDATER=1): show a borderless progress window, copy this
 *                     AppImage's payload ($APPDIR) into the install dir while
 *                     preserving user data, then swap directories.
 *
 * Usage: <appimage> [install_dir]
 *   install_dir defaults to <dir of the AppImage>/beacon.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define APP_BINARY   "usr/bin/Beacon"
#define VERSION_FILE "version.txt"

/* User data that must survive an update: never delete or overwrite these. */
static const char *kPreserveDirs[] = {
    ".minecraft", "auth", "cache", "instances", "generic"
};
static const char *kPreserveFiles[] = {
    "settings.ini"
};

/* ---- Locale-aware strings ---- */
static int g_zh = 1;
static const char *L_TITLE;
static const char *L_UPDATING;
static const char *L_ERR_EXTRACT;
static const char *L_ERR_NOTFOUND;
static const char *L_ERR_LAUNCH;
static const char *L_OK;

static void init_lang(void) {
    const char *lang = getenv("LANG");
    if (!lang) lang = getenv("LC_ALL");
    g_zh = (lang && strstr(lang, "zh")) ? 1 : 0;
    if (g_zh) {
        L_TITLE         = "Beacon";
        L_UPDATING      = "正在更新 Beacon，请稍候...";
        L_ERR_EXTRACT   = "解压失败，请检查磁盘空间后重试。";
        L_ERR_NOTFOUND  = "Beacon 未找到，安装可能不完整。";
        L_ERR_LAUNCH    = "启动 Beacon 失败。";
        L_OK            = "确定";
    } else {
        L_TITLE         = "Beacon";
        L_UPDATING      = "Updating Beacon, please wait...";
        L_ERR_EXTRACT   = "Extraction failed. Check disk space and try again.";
        L_ERR_NOTFOUND  = "Beacon not found. The installation may be incomplete.";
        L_ERR_LAUNCH    = "Failed to start Beacon.";
        L_OK            = "OK";
    }
}

static char g_log_path[4096];

static void log_msg(const char *fmt, ...) {
    FILE *f = fopen(g_log_path, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

/* dirname without libgen.h (which would mutate its argument). */
static void path_dirname(char *out, size_t cap, const char *path) {
    snprintf(out, cap, "%s", path);
    char *s = strrchr(out, '/');
    if (!s) { snprintf(out, cap, "."); return; }
    if (s == out) { out[1] = '\0'; return; }
    *s = '\0';
}

static int has_suffix(const char *name, const char *suffix) {
    size_t n = strlen(name), sl = strlen(suffix);
    if (sl > n) return 0;
    return strcmp(name + n - sl, suffix) == 0;
}

/* Root-level entries never copied into the install dir. */
static int is_skip_root(const char *name) {
    if (strcmp(name, "AppRun") == 0) return 1;
    if (strcmp(name, "Beacon") == 0) return 1;          /* convenience symlink */
    if (strcmp(name, VERSION_FILE) == 0) return 1;      /* written explicitly */
    if (has_suffix(name, ".desktop")) return 1;
    if (has_suffix(name, ".svg") || has_suffix(name, ".png") ||
        has_suffix(name, ".xpm") || has_suffix(name, ".ico")) return 1;
    return 0;
}

static int is_preserved_name(const char *name) {
    size_t i;
    for (i = 0; i < sizeof(kPreserveDirs) / sizeof(kPreserveDirs[0]); i++)
        if (strcmp(name, kPreserveDirs[i]) == 0) return 1;
    for (i = 0; i < sizeof(kPreserveFiles) / sizeof(kPreserveFiles[0]); i++)
        if (strcmp(name, kPreserveFiles[i]) == 0) return 1;
    return 0;
}

/* ---- Filesystem helpers ---- */

static void pump_gtk(void) {
    while (gtk_events_pending()) gtk_main_iteration();
}

static gint64 tree_size(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) return 0;
    if (S_ISDIR(st.st_mode)) {
        gint64 sum = 0;
        DIR *d = opendir(path);
        if (!d) return 0;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char p[4096];
            snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
            sum += tree_size(p);
        }
        closedir(d);
        return sum;
    }
    return (gint64)st.st_size;
}

static void delete_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) { unlink(path); return; }
    DIR *d = opendir(path);
    if (!d) { rmdir(path); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char p[4096];
        snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        delete_tree(p);
    }
    closedir(d);
    rmdir(path);
}

typedef struct {
    GtkWidget *bar;
    gint64 total;
    gint64 done;
} CopyCtx;

/* Copy one regular file or symlink. Returns 1 on success. */
static int copy_file(const char *src, const char *dst, CopyCtx *ctx) {
    struct stat st;
    if (lstat(src, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) {
        char link[4096];
        ssize_t n = readlink(src, link, sizeof(link) - 1);
        if (n < 0) return 0;
        link[n] = '\0';
        symlink(link, dst);
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, st.st_mode & 07777);
        return 1;
    }
    FILE *fi = fopen(src, "rb");
    if (!fi) return 0;
    FILE *fo = fopen(dst, "wb");
    if (!fo) { fclose(fi); return 0; }
    char buf[65536];
    size_t nr;
    while ((nr = fread(buf, 1, sizeof(buf), fi)) > 0) {
        if (fwrite(buf, 1, nr, fo) != nr) { fclose(fi); fclose(fo); return 0; }
        ctx->done += (gint64)nr;
        if (ctx->bar) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->bar),
                ctx->total > 0 ? (gdouble)ctx->done / (gdouble)ctx->total : 0.0);
            pump_gtk();
        }
    }
    fclose(fi);
    fclose(fo);
    chmod(dst, st.st_mode & 07777);
    return 1;
}

static int copy_tree(const char *src, const char *dst, CopyCtx *ctx) {
    struct stat st;
    if (lstat(src, &st) != 0) return 0;
    if (S_ISLNK(st.st_mode)) return copy_file(src, dst, ctx);
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, 0755) != 0 && errno != EEXIST) return 0;
        DIR *d = opendir(src);
        if (!d) return 0;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (is_preserved_name(e->d_name)) continue; /* never copy user data */
            char s[4096], t[4096];
            snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
            snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);
            if (!copy_tree(s, t, ctx)) { closedir(d); return 0; }
        }
        closedir(d);
        return 1;
    }
    return copy_file(src, dst, ctx);
}

/* Move preserved user dirs/files from old install into the fresh one. */
static int preserve_user_data(const char *old_install, const char *new_install) {
    char src[4096], dst[4096];
    size_t i;
    for (i = 0; i < sizeof(kPreserveDirs) / sizeof(kPreserveDirs[0]); i++) {
        snprintf(src, sizeof(src), "%s/%s", old_install, kPreserveDirs[i]);
        snprintf(dst, sizeof(dst), "%s/%s", new_install, kPreserveDirs[i]);
        if (access(src, F_OK) != 0) continue;
        log_msg("Preserving dir: %s", src);
        delete_tree(dst);
        if (rename(src, dst) != 0) {
            log_msg("ERROR: failed to move preserved dir %s", src);
            return 0;
        }
    }
    for (i = 0; i < sizeof(kPreserveFiles) / sizeof(kPreserveFiles[0]); i++) {
        snprintf(src, sizeof(src), "%s/%s", old_install, kPreserveFiles[i]);
        snprintf(dst, sizeof(dst), "%s/%s", new_install, kPreserveFiles[i]);
        if (access(src, F_OK) != 0) continue;
        log_msg("Preserving file: %s", src);
        unlink(dst);
        if (rename(src, dst) != 0) {
            log_msg("ERROR: failed to move preserved file %s", src);
            return 0;
        }
    }
    return 1;
}

/* ---- UI ---- */

static void show_error(const char *text) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_title(GTK_WINDOW(win), L_TITLE);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(win), box);

    GtkWidget *lbl = gtk_label_new(text);
    gtk_widget_set_margin_start(lbl, 20);
    gtk_widget_set_margin_end(lbl, 20);
    gtk_widget_set_margin_top(lbl, 20);
    gtk_box_pack_start(GTK_BOX(box), lbl, TRUE, TRUE, 0);

    GtkWidget *btn = gtk_button_new_with_label(L_OK);
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_widget_destroy), win);
    gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
    gtk_main();
}

/* ---- UPDATE mode ---- */

static int do_update(const char *appdir, const char *install_dir) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_title(GTK_WINDOW(win), L_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(win), 380, 120);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(win), box);

    GtkWidget *label = gtk_label_new(L_UPDATING);
    gtk_widget_set_margin_top(label, 16);
    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 20);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    GtkWidget *bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), 0.0);
    gtk_widget_set_margin_start(bar, 20);
    gtk_widget_set_margin_end(bar, 20);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
    pump_gtk();

    char tmp_dir[4096];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s.tmp", install_dir);
    log_msg("Updating into %s (appdir=%s)", install_dir, appdir);

    delete_tree(tmp_dir);
    if (mkdir(tmp_dir, 0755) != 0 && errno != EEXIST) {
        log_msg("ERROR: mkdir %s failed: %s", tmp_dir, strerror(errno));
        gtk_widget_destroy(win);
        show_error(L_ERR_EXTRACT);
        return 1;
    }

    /* Copy every root entry of the payload except the skip list. */
    CopyCtx ctx = { bar, tree_size(appdir), 0 };
    DIR *d = opendir(appdir);
    int ok = 1;
    if (!d) ok = 0;
    if (d) {
        struct dirent *e;
        while (ok && (e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (is_skip_root(e->d_name)) continue;
            char s[4096], t[4096];
            snprintf(s, sizeof(s), "%s/%s", appdir, e->d_name);
            snprintf(t, sizeof(t), "%s/%s", tmp_dir, e->d_name);
            if (!copy_tree(s, t, &ctx)) {
                log_msg("ERROR: copy failed for %s", s);
                ok = 0;
                break;
            }
        }
        closedir(d);
    }

    /* Write version.txt into the fresh install. */
    if (ok) {
        char vsrc[4096], vdst[4096];
        snprintf(vsrc, sizeof(vsrc), "%s/%s", appdir, VERSION_FILE);
        snprintf(vdst, sizeof(vdst), "%s/%s", tmp_dir, VERSION_FILE);
        FILE *fi = fopen(vsrc, "r");
        FILE *fo = fopen(vdst, "w");
        char buf[128];
        if (fi && fo && fgets(buf, sizeof(buf), fi)) fputs(buf, fo);
        if (fi) fclose(fi);
        if (fo) fclose(fo);
    }

    /* Move user data from the old install, then swap directories.
     * Once we start moving preserved data we must NOT delete tmp_dir on
     * failure: it may already hold preserved user data. */
    int mutating = 0;
    if (ok && access(install_dir, F_OK) == 0) {
        mutating = 1;
        ok = preserve_user_data(install_dir, tmp_dir);
        if (ok) {
            log_msg("Removing old install: %s", install_dir);
            delete_tree(install_dir);
        }
    }
    if (ok && rename(tmp_dir, install_dir) != 0) {
        log_msg("ERROR: swap failed: %s", strerror(errno));
        ok = 0;
    }

    if (!ok) {
        if (mutating)
            log_msg("ERROR: %s left in place; it may contain preserved user data", tmp_dir);
        else
            delete_tree(tmp_dir);
        gtk_widget_destroy(win);
        show_error(L_ERR_EXTRACT);
        return 1;
    }

    log_msg("Update complete");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), 1.0);
    pump_gtk();
    g_usleep(250 * 1000);
    gtk_widget_destroy(win);
    while (gtk_events_pending()) gtk_main_iteration();
    return 0;
}

/* ---- RUN mode ---- */

static void prepend_env(const char *name, const char *dir) {
    const char *old = getenv(name);
    char buf[8192];
    if (old && *old)
        snprintf(buf, sizeof(buf), "%s:%s", dir, old);
    else
        snprintf(buf, sizeof(buf), "%s", dir);
    setenv(name, buf, 1);
}

static int run_app(const char *install_dir, int argc, char **argv) {
    char bin[4096];
    snprintf(bin, sizeof(bin), "%s/%s", install_dir, APP_BINARY);
    if (access(bin, X_OK) != 0) {
        log_msg("ERROR: %s not found", bin);
        gtk_init(&argc, &argv);
        show_error(L_ERR_NOTFOUND);
        return 1;
    }

    /* Replicate the runtime env linuxdeploy's AppRun would have set. */
    char d[4096];
    snprintf(d, sizeof(d), "%s/usr/lib", install_dir);
    if (access(d, F_OK) == 0) prepend_env("LD_LIBRARY_PATH", d);

    {
        static const char *pluginCands[] = {
            "usr/plugins", "usr/lib/qt6/plugins", "usr/lib/qt5/plugins", "usr/lib/qt/plugins"
        };
        size_t i;
        for (i = 0; i < sizeof(pluginCands) / sizeof(pluginCands[0]); i++) {
            snprintf(d, sizeof(d), "%s/%s", install_dir, pluginCands[i]);
            if (access(d, F_OK) == 0) prepend_env("QT_PLUGIN_PATH", d);
        }
    }
    snprintf(d, sizeof(d), "%s/usr/qml", install_dir);
    if (access(d, F_OK) == 0) prepend_env("QML2_IMPORT_PATH", d);
    snprintf(d, sizeof(d), "%s/usr/bin", install_dir);
    if (access(d, F_OK) == 0) prepend_env("PATH", d);
    snprintf(d, sizeof(d), "%s/usr/share", install_dir);
    if (access(d, F_OK) == 0) prepend_env("XDG_DATA_DIRS", d);

    log_msg("Launching: %s", bin);

    /* argv[1] was the install dir; forward the remaining args to the app. */
    char **new_argv = malloc((size_t)(argc + 1) * sizeof(char *));
    if (!new_argv) return 1;
    new_argv[0] = bin;
    int j = 1;
    for (int i = 2; i < argc; i++) new_argv[j++] = argv[i];
    new_argv[j] = NULL;

    execv(bin, new_argv);

    gtk_init(&argc, &argv);
    show_error(L_ERR_LAUNCH);
    return 1;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    init_lang();

    const char *appdir = getenv("APPDIR");
    static char appdir_buf[4096];
    if (!appdir || !*appdir) {
        path_dirname(appdir_buf, sizeof(appdir_buf), argv[0]);
        appdir = appdir_buf;
    }

    const char *updater = getenv("BEACON_UPDATER");
    int is_update = (updater && *updater && strcmp(updater, "0") != 0);

    /* Resolve the install dir: argv[1] wins, else <dir of AppImage>/beacon. */
    static char install_buf[4096];
    const char *install_dir = NULL;
    if (argc >= 2 && argv[1] && *argv[1]) {
        install_dir = argv[1];
    } else {
        const char *ai = getenv("APPIMAGE");
        static char base_buf[4096];
        if (ai && *ai) {
            path_dirname(base_buf, sizeof(base_buf), ai);
        } else if (strchr(argv[0], '/')) {
            /* AppRun invoked straight from an unpacked AppDir: install beside it. */
            static char tmp_buf[4096];
            path_dirname(tmp_buf, sizeof(tmp_buf), argv[0]);
            path_dirname(base_buf, sizeof(base_buf), tmp_buf);
        } else {
            const char *home = getenv("HOME");
            snprintf(base_buf, sizeof(base_buf), "%s/.local/share",
                     home && *home ? home : "/tmp");
        }
        snprintf(install_buf, sizeof(install_buf), "%s/beacon", base_buf);
        install_dir = install_buf;
    }

    snprintf(g_log_path, sizeof(g_log_path), "%s_install.log", install_dir);
    log_msg("Beacon self-extractor started (update=%d)", is_update);

    if (is_update) {
        gtk_init(&argc, &argv);
        return do_update(appdir, install_dir);
    }
    return run_app(install_dir, argc, argv);
}
