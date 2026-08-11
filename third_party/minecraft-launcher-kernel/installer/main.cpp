/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include <mcbase.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

static const char *g_mirror = nullptr;
static long g_timeout_ms = 120000;

static void print_help(void) {
    mc_console_printf("installer - %s\n", mc_i18n("installer_desc"));
    mc_console_printf("%s: installer <version-id> --loader <type> [%s]\n\n", mc_i18n("usage"), mc_i18n("options"));
    mc_console_printf("%s:\n", mc_i18n("commands"));
    mc_console_printf("  <version-id>  Minecraft %s\n", mc_i18n("version"));
    mc_console_printf("\n%s:\n", mc_i18n("options"));
    mc_console_printf("  --loader <type>   %s: forge/fabric/quilt/neoforge/optifine/liteloader\n", mc_i18n("loader_type"));
    mc_console_printf("  --loader-ver <ver> %s\n", mc_i18n("loader_ver_opt"));
    mc_console_printf("  --java <path>     %s\n", mc_i18n("java_opt"));
    mc_console_printf("  --dir <path>      %s\n", mc_i18n("dir_opt"));
    mc_console_printf("  --mirror <type>   %s\n", mc_i18n("mirror"));
    mc_console_printf("  --lang <code>     %s\n", mc_i18n("lang_opt"));
    mc_console_printf("  --json            %s\n", mc_i18n("json_opt"));
    mc_console_printf("  --debug           %s\n", mc_i18n("debug_opt"));
    mc_console_printf("\n%s:\n", mc_i18n("examples"));
    mc_console_printf("  installer 1.20.1 --loader forge --loader-ver 47.4.9 --java /path/to/java\n");
    mc_console_printf("  installer 1.20.1 --loader fabric --dir mymc --java /path/to/java\n");
}

// ---- Version helpers ----
static McVersion *new_version(void) {
    McVersion *v = static_cast<McVersion*>(malloc(sizeof(McVersion)));
    if (v) mc_version_init(v);
    return v;
}

static void del_version(McVersion *v) {
    if (v) { mc_version_free(v); free(v); }
}

static void make_loader_version_id(const char *prefix, const char *loader_ver, const char *mc_ver,
                                    McVersion *v, QJsonObject &raw_json) {
    if (strcmp(v->id, mc_ver) == 0 ||
        (v->inherits_from[0] && strcmp(v->id, v->inherits_from) == 0)) {
        char new_id[64];
        if (loader_ver && *loader_ver)
            snprintf(new_id, sizeof(new_id), "%s-%s-%s", prefix, loader_ver, mc_ver);
        else
            snprintf(new_id, sizeof(new_id), "%s-%s", prefix, mc_ver);
        raw_json["id"] = QString(new_id);
        strncpy(v->id, new_id, sizeof(v->id) - 1);
    }
}

static void save_version_profile(McVersion *v, const char *mc_dir) {
    char versions_dir[MC_PATH_MAX];
    mc_path_join(mc_dir, "versions", versions_dir, sizeof(versions_dir));
    mc_path_mkdir_p(versions_dir);
    char ver_dir[MC_PATH_MAX];
    mc_path_join(versions_dir, v->id, ver_dir, sizeof(ver_dir));
    mc_path_mkdir_p(ver_dir);
    char json_path[MC_PATH_MAX];
    snprintf(json_path, sizeof(json_path), "%s/%s.json", ver_dir, v->id);
    if (!v->raw_json.isEmpty()) {
        QJsonDocument doc(v->raw_json);
        QByteArray json_bytes = doc.toJson(QJsonDocument::Indented);
        FILE *f = fopen(json_path, "wb");
        if (f) { fwrite(json_bytes.constData(), 1, json_bytes.size(), f); fclose(f); }
    }
    mc_info("Profile saved: %s/%s.json", ver_dir, v->id);
}

// ---- Forge installer (handles both old and new Forge) ----
static void ensure_launcher_profiles(const char *mc_dir) {
    char path[MC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/launcher_profiles.json", mc_dir);
    if (mc_path_exists(path)) return;
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("{\"profiles\":{},\"selectedProfile\":\"(Default)\",\"clientToken\":\"00000000-0000-0000-0000-000000000000\",\"launcherVersion\":{\"format\":21,\"name\":\"1.0.0\"}}", f);
        fclose(f);
        mc_info("Created launcher_profiles.json");
    }
}

static int run_installer_jar(const char *java_path, const char *installer_jar, const char *mc_dir) {
    ensure_launcher_profiles(mc_dir);
    if (!mc_path_exists(java_path)) {
        mc_error("Java not found: %s", java_path);
        return 0;
    }
    mc_info("Running installer: java -jar \"%s\" --installClient \"%s\"", installer_jar, mc_dir);
    QProcess proc;
    QStringList args;
    args << "-jar" << QString::fromUtf8(installer_jar)
         << "--installClient" << QString::fromUtf8(mc_dir);
    proc.start(QString::fromUtf8(java_path), args);
    if (!proc.waitForFinished(300000)) {
        mc_warn("Installer timed out");
        return 0;
    }
    QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QString err = QString::fromLocal8Bit(proc.readAllStandardError());
    if (proc.exitCode() == 0) {
        if (!out.isEmpty()) mc_info("Installer output: %s", out.toUtf8().constData());
        mc_info("Installer completed successfully");
        return 1;
    }
    if (!out.isEmpty()) mc_info("Installer output: %s", out.toUtf8().constData());
    if (!err.isEmpty()) mc_debug("Installer stderr: %s", err.toUtf8().constData());
    mc_error("Installer failed (exit code %d)", proc.exitCode());
    return 0;
}

static int download_file(const char *url, const char *output_path) {
    return mc_qt_download_file(url, output_path, nullptr, 0, g_timeout_ms);
}

// ---- Fabric ----
static int install_fabric(const char *mc_ver, const char *loader_ver, const char *mc_dir, const char *java_path) {
    mc_info("Installing Fabric loader for MC %s...", mc_ver);
    char loader_ver_buf[64] = "";
    if (loader_ver && *loader_ver) {
        strncpy(loader_ver_buf, loader_ver, sizeof(loader_ver_buf) - 1);
        loader_ver = loader_ver_buf;
    } else {
        char list_url[512];
        snprintf(list_url, sizeof(list_url), "https://meta.fabricmc.net/v2/versions/loader/%s", mc_ver);
        McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
        McHttpResponse *resp = mc_http_get(&client, list_url);
        if (resp && resp->success && resp->data) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray arr = doc.array();
                if (!arr.isEmpty()) {
                    QJsonObject first = arr[0].toObject();
                    QJsonObject loader_obj = first.value("loader").toObject();
                    QString latest = loader_obj.value("version").toString();
                    if (!latest.isEmpty()) {
                        strncpy(loader_ver_buf, latest.toUtf8().constData(), sizeof(loader_ver_buf) - 1);
                        loader_ver = loader_ver_buf;
                    }
                }
            }
        }
        if (resp) mc_http_response_free(resp);
        mc_info("Fabric loader version: %s", loader_ver && *loader_ver ? loader_ver : "(latest)");
    }

    char url[512];
    if (loader_ver && *loader_ver)
        snprintf(url, sizeof(url), "https://meta.fabricmc.net/v2/versions/loader/%s/%s/profile/json", mc_ver, loader_ver);
    else
        snprintf(url, sizeof(url), "https://meta.fabricmc.net/v2/versions/loader/%s/profile/json", mc_ver);

    McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch Fabric profile");
        if (resp) { mc_http_response_free(resp); } return 1;
    }

    McVersion *v = new_version();
    if (!v) { mc_http_response_free(resp); return 1; }
    if (!mc_version_parse(v, resp->data)) {
        mc_error("Failed to parse Fabric profile JSON");
        mc_http_response_free(resp); del_version(v); return 1;
    }
    make_loader_version_id("fabric-loader", loader_ver, mc_ver, v, v->raw_json);
    save_version_profile(v, mc_dir);

    // Print missing library URLs for manual download
    char lib_dir[MC_PATH_MAX];
    mc_path_join(mc_dir, "libraries", lib_dir, sizeof(lib_dir));
    for (int i = 0; i < v->library_count; i++) {
        McLibrary *lib = &v->libraries[i];
        if (!lib->is_required) continue;
        if (lib->is_natives && lib->classifier_url[0]) continue;
        char rel[MC_PATH_MAX], lib_url[2048], full[MC_PATH_MAX];
        mc_library_resolve_path(lib->name, rel, sizeof(rel));
        if (!rel[0]) continue;
        mc_path_join(lib_dir, rel, full, sizeof(full));
        if (mc_path_exists(full)) continue;
        size_t ulen = strlen(lib->url);
        if (ulen > 4 && memcmp(lib->url + ulen - 4, ".jar", 4) == 0)
            strncpy(lib_url, lib->url, sizeof(lib_url) - 1);
        else
            mc_library_resolve_url(lib->name, lib->url[0] ? lib->url : nullptr, lib_url, sizeof(lib_url));
        if (!lib_url[0]) continue;
        mc_info("Missing library: %s -> %s", lib->name, lib_url);
    }

    mc_http_response_free(resp);
    del_version(v);
    mc_info("Fabric installed: %s-%s-%s", mc_ver, loader_ver, mc_ver);
    return 0;
}

// ---- Forge ----
// Try to fetch Forge profile JSON from all known mirror paths.
// Returns 1 on success, 0 if all mirrors failed.
static int forge_fetch_profile_json(const char *mc_ver, const char *file_ver,
                                     McVersion *v, char *out_url, size_t out_url_size) {
    // Mirror list: primary + fallbacks
    const char *mirror_bases[] = {
        "https://bmclapi2.bangbang93.com/maven",
        "https://maven.minecraftforge.net",
        "https://files.minecraftforge.net/maven",
        nullptr
    };
    for (int m = 0; mirror_bases[m]; m++) {
        char profile_url[512];
        snprintf(profile_url, sizeof(profile_url),
                 "%s/net/minecraftforge/forge/%s-%s/forge-%s-%s-client.json",
                 mirror_bases[m], mc_ver, file_ver, mc_ver, file_ver);
        if (mc_version_fetch(v, profile_url)) {
            if (out_url) strncpy(out_url, profile_url, out_url_size);
            return 1;
        }
    }
    return 0;
}

static int install_forge(const char *mc_ver, const char *forge_ver, const char *mc_dir, const char *java_path) {
    mc_info("Installing Forge for MC %s...", mc_ver);
    char url[512];
    snprintf(url, sizeof(url), "https://bmclapi2.bangbang93.com/forge/minecraft/%s", mc_ver);
    McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch Forge version list for MC %s", mc_ver);
        if (resp) { mc_http_response_free(resp); } return 1;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data));
    mc_http_response_free(resp);
    if (!doc.isArray()) { mc_error("Invalid Forge version list"); return 1; }
    QJsonArray versions = doc.array();

    const char *selected = nullptr;
    char selected_buf[128] = "";
    int n = versions.size();
    for (int i = 0; i < n; i++) {
        QJsonObject entry = versions[i].toObject();
        QString ver = entry.value("version").toString();
        if (!forge_ver || !*forge_ver) {
            if (!selected || strcmp(ver.toUtf8().constData(), selected) > 0) {
                const char *v = ver.toUtf8().constData();
                strncpy(selected_buf, v, sizeof(selected_buf) - 1);
                selected = selected_buf;
            }
        } else if (strcmp(ver.toUtf8().constData(), forge_ver) == 0) {
            strncpy(selected_buf, ver.toUtf8().constData(), sizeof(selected_buf) - 1);
            selected = selected_buf;
            break;
        }
    }
    if (!selected) { mc_error("No Forge version found"); return 1; }
    mc_info("Selected Forge version: %s", selected);
    const char *file_ver = selected;

    // Primary: download profile JSON directly
    mc_info("Trying Forge profile JSON...");
    McVersion *pv = new_version();
    if (pv) {
        char profile_url[512] = "";
        if (forge_fetch_profile_json(mc_ver, file_ver, pv, profile_url, sizeof(profile_url))) {
            make_loader_version_id("forge", file_ver, mc_ver, pv, pv->raw_json);
            save_version_profile(pv, mc_dir);
            del_version(pv);
            mc_info("Forge profile saved: %s", profile_url);
            return 0;
        }
        del_version(pv);
    }

    // Fallback: download and run installer JAR
    mc_warn("Profile JSON not found, trying installer JAR...");
    char mc_safe[64], dl_url[512], output_path[MC_PATH_MAX];
    strncpy(mc_safe, mc_ver, sizeof(mc_safe) - 1);
    for (char *p = mc_safe; *p; p++) if (*p == '.') *p = '_';

    snprintf(dl_url, sizeof(dl_url), "https://bmclapi2.bangbang93.com/maven/net/minecraftforge/forge/%s-%s/forge-%s-%s-installer.jar", mc_ver, file_ver, mc_ver, file_ver);
    snprintf(output_path, sizeof(output_path), "%s/forge-%s-%s-installer.jar", mc_dir, mc_safe, file_ver);
    if (!download_file(dl_url, output_path)) {
        snprintf(dl_url, sizeof(dl_url), "https://files.minecraftforge.net/maven/net/minecraftforge/forge/%s-%s/forge-%s-%s-installer.jar", mc_ver, file_ver, mc_ver, file_ver);
        if (!download_file(dl_url, output_path)) {
            mc_error("Failed to download Forge JAR");
            mc_warn("Try installing Forge manually. See: https://files.minecraftforge.net");
            return 1;
        }
    }
    mc_info("Forge installer downloaded: %s", output_path);

    int installed = run_installer_jar(java_path, output_path, mc_dir);
    if (installed) {
        mc_info("Forge installer completed");
        return 0;
    }

    mc_warn("Forge installer failed to run. Try manually:");
    mc_warn("  java -jar \"%s\" --installClient \"%s\"", output_path, mc_dir);
    return 1;
}

// ---- Quilt ----
static int install_quilt(const char *mc_ver, const char *loader_ver, const char *mc_dir, const char *java_path) {
    mc_info("Installing Quilt loader for MC %s...", mc_ver);
    char loader_ver_buf[64] = "";
    if (loader_ver && *loader_ver) {
        strncpy(loader_ver_buf, loader_ver, sizeof(loader_ver_buf) - 1);
        loader_ver = loader_ver_buf;
    } else {
        char list_url[512];
        snprintf(list_url, sizeof(list_url), "https://meta.quiltmc.org/v3/versions/loader/%s", mc_ver);
        McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
        McHttpResponse *resp = mc_http_get(&client, list_url);
        if (resp && resp->success && resp->data) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray arr = doc.array();
                if (!arr.isEmpty()) {
                    QJsonObject first = arr[0].toObject();
                    QJsonObject loader_obj = first.value("loader").toObject();
                    QString latest = loader_obj.value("version").toString();
                    if (!latest.isEmpty()) {
                        strncpy(loader_ver_buf, latest.toUtf8().constData(), sizeof(loader_ver_buf) - 1);
                        loader_ver = loader_ver_buf;
                    }
                }
            }
        }
        mc_http_response_free(resp);
        mc_info("Quilt loader version: %s", loader_ver && *loader_ver ? loader_ver : "(latest)");
    }

    char url[512];
    if (loader_ver && *loader_ver)
        snprintf(url, sizeof(url), "https://meta.quiltmc.org/v3/versions/loader/%s/%s/profile/json", mc_ver, loader_ver);
    else
        snprintf(url, sizeof(url), "https://meta.quiltmc.org/v3/versions/loader/%s/profile/json", mc_ver);

    McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch Quilt profile");
        if (resp) { mc_http_response_free(resp); } return 1;
    }

    McVersion *v = new_version();
    if (!v) { mc_http_response_free(resp); return 1; }
    if (!mc_version_parse(v, resp->data)) {
        mc_error("Failed to parse Quilt profile JSON");
        mc_http_response_free(resp); del_version(v); return 1;
    }
    make_loader_version_id("quilt-loader", loader_ver, mc_ver, v, v->raw_json);
    save_version_profile(v, mc_dir);
    mc_http_response_free(resp);
    del_version(v);
    mc_info("Quilt installed: %s", v->id);
    return 0;
}

// ---- NeoForge ----
static int install_neoforge(const char *mc_ver, const char *nforge_ver, const char *mc_dir, const char *java_path) {
    mc_info("Installing NeoForge for MC %s...", mc_ver);
    int is_legacy = (strcmp(mc_ver, "1.20.1") == 0);
    const char *pkg = is_legacy ? "forge" : "neoforge";

    char list_url[512];
    snprintf(list_url, sizeof(list_url), "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/%s", pkg);
    McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, list_url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch NeoForge version list");
        if (resp) { mc_http_response_free(resp); } return 1;
    }

    QJsonParseError err;
    QJsonDocument json_doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError) { mc_error("Invalid NeoForge version list JSON"); return 1; }
    QJsonObject json = json_doc.object();

    char version_buf[128] = "";
    if (nforge_ver && *nforge_ver) {
        strncpy(version_buf, nforge_ver, sizeof(version_buf) - 1);
    } else {
        // Select latest version that matches this MC version
        QJsonArray versions_arr = json.value("versions").toArray();
        QString mcPrefix = QString::fromUtf8(mc_ver) + "-";
        for (int i = 0; i < versions_arr.size(); i++) {
            QJsonValue item = versions_arr[i];
            if (item.isString()) {
                QString v = item.toString();
                if (v.startsWith(mcPrefix))
                    strncpy(version_buf, v.toUtf8().constData(), sizeof(version_buf) - 1);
            }
        }
    }
    if (!version_buf[0]) { mc_error("No NeoForge version found for MC %s", mc_ver); return 1; }
    mc_info("NeoForge version: %s", version_buf);

    // NeoForge does not publish -client.json files; use installer JAR directly
    mc_info("Downloading NeoForge installer JAR...");
    char dl_url[1024], output_path[MC_PATH_MAX];
    snprintf(dl_url, sizeof(dl_url),
             "https://maven.neoforged.net/releases/net/neoforged/%s/%s/%s-%s-installer.jar",
             pkg, version_buf, pkg, version_buf);
    snprintf(output_path, sizeof(output_path), "%s/neoforge-%s-installer.jar", mc_dir, version_buf);
    if (!download_file(dl_url, output_path)) {
        // If bare version fails, try with MC prefix (e.g. "47.1.82" -> "1.20.1-47.1.82")
        if (!nforge_ver || strncmp(version_buf, mc_ver, strlen(mc_ver)) != 0) {
            char alt_ver[128];
            snprintf(alt_ver, sizeof(alt_ver), "%s-%s", mc_ver, version_buf);
            char alt_url[1024];
            snprintf(alt_url, sizeof(alt_url),
                     "https://maven.neoforged.net/releases/net/neoforged/%s/%s/%s-%s-installer.jar",
                     pkg, alt_ver, pkg, alt_ver);
            char alt_output[MC_PATH_MAX];
            snprintf(alt_output, sizeof(alt_output), "%s/neoforge-%s-installer.jar", mc_dir, alt_ver);
            if (download_file(alt_url, alt_output)) {
                mc_info("NeoForge installer saved: %s", alt_output);
                strncpy(output_path, alt_output, sizeof(output_path) - 1);
                strncpy(version_buf, alt_ver, sizeof(version_buf) - 1);
                goto run_installer;
            }
        }
        mc_error("Failed to download NeoForge installer");
        return 1;
    }

run_installer:
    mc_info("NeoForge installer saved: %s", output_path);
    int installed = run_installer_jar(java_path, output_path, mc_dir);
    if (installed) {
        mc_info("NeoForge installer completed");
        return 0;
    }
    mc_warn("NeoForge installer could not be run automatically");
    mc_warn("Run it manually: java -jar \"%s\" --installClient \"%s\"", output_path, mc_dir);
    return 0;
}

// ---- OptiFine ----
static int install_optifine(const char *mc_ver, const char *mc_dir, const char *java_path) {
    mc_info("Installing OptiFine for MC %s...", mc_ver);
    char url[512];
    snprintf(url, sizeof(url), "https://bmclapi2.bangbang93.com/optifine/versionList");
    McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch OptiFine version list");
        if (resp) { mc_http_response_free(resp); } return 1;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
    mc_http_response_free(resp);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        mc_error("Invalid OptiFine version list"); return 1;
    }
    QJsonArray list = doc.array();

    QByteArray filenameBytes;
    for (int i = 0; i < list.size(); i++) {
        QJsonObject entry = list[i].toObject();
        QString ver = entry.value("mcversion").toString();
        if (ver == QString::fromUtf8(mc_ver)) {
            filenameBytes = entry.value("filename").toString().toUtf8();
            break;
        }
    }
    if (filenameBytes.isEmpty()) { mc_error("No OptiFine found for MC %s", mc_ver); return 1; }
    const char *filename = filenameBytes.constData();
    mc_info("OptiFine: %s", filename);

    char dl_url[512], dl_url2[512], output_path[MC_PATH_MAX];
    snprintf(dl_url, sizeof(dl_url), "https://bmclapi2.bangbang93.com/optifine/download?f=%s", filename);
    snprintf(dl_url2, sizeof(dl_url2), "https://optifine.net/download?f=%s", filename);
    snprintf(output_path, sizeof(output_path), "%s/%s", mc_dir, filename);
    if (!download_file(dl_url, output_path))
        if (!download_file(dl_url2, output_path))
            { mc_error("Failed to download OptiFine"); return 1; }
    mc_info("OptiFine saved: %s", output_path);

    // OptiFine doesn't support CLI extraction on this version.
    // Copy the installer JAR to mods/ (for Forge) and notify user.
    char mods_dir[MC_PATH_MAX];
    snprintf(mods_dir, sizeof(mods_dir), "%s/mods", mc_dir);
    mc_path_mkdir(mods_dir);
    char mod_path[MC_PATH_MAX];
    snprintf(mod_path, sizeof(mod_path), "%s/%s", mods_dir, filename);
    if (mc_path_exists(output_path)) {
        QFile::copy(QString::fromUtf8(output_path), QString::fromUtf8(mod_path));
    }
    mc_info("OptiFine downloaded to: %s", output_path);
    mc_info("For Forge users: the file has been copied to mods/ folder.");
    mc_info("For standalone use, run the GUI installer:");
    mc_warn("  java -jar \"%s\"", output_path);
    return 0;
}

// ---- LiteLoader ----
static int install_liteloader(const char *mc_ver, const char *mc_dir, const char *java_path) {
    mc_info("Installing LiteLoader for MC %s...", mc_ver);
    char url[512];
    snprintf(url, sizeof(url), "https://dl.liteloader.com/versions/versions.json");
    McHttpClient client; mc_http_init(&client); mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        snprintf(url, sizeof(url), "https://bmclapi2.bangbang93.com/maven/com/mumfrey/liteloader/versions.json");
        mc_http_response_free(resp);
        resp = mc_http_get(&client, url);
        if (!resp || !resp->success || !resp->data) {
            mc_error("Failed to fetch LiteLoader versions");
            if (resp) { mc_http_response_free(resp); } return 1;
        }
    }
    mc_info("LiteLoader version data fetched");

    QJsonParseError err;
    QJsonDocument versions_doc = QJsonDocument::fromJson(QByteArray(resp->data), &err);
    int result = 0;
    if (err.error == QJsonParseError::NoError && versions_doc.isObject()) {
        QJsonObject versions_json = versions_doc.object();
        QJsonValue versions_val = versions_json.value("versions");
        if (versions_val.isObject()) {
            QJsonObject versions_obj = versions_val.toObject();
            QJsonValue mc_ver_val = versions_obj.value(QString::fromUtf8(mc_ver));
            if (mc_ver_val.isObject()) {
                QJsonObject mc_ver_obj = mc_ver_val.toObject();
                QJsonValue repo_val = mc_ver_obj.value("repo");
                if (repo_val.isObject()) {
                    QJsonObject repo = repo_val.toObject();
                    QJsonValue liteloader_val = repo.value("com/mumfrey/liteloader");
                    if (liteloader_val.isObject()) {
                        QJsonObject liteloader_obj = liteloader_val.toObject();
                        for (auto it = liteloader_obj.begin(); it != liteloader_obj.end(); ++it) {
                            if (it.key().contains("-release.json") || it.key().contains("-release.jar")) {
                                QJsonObject child = it.value().toObject();
                                QString dl_url = child.value("url").toString();
                                if (dl_url.isEmpty()) continue;
                                char profile_url[512];
                                QByteArray dlUrlBytes = dl_url.toUtf8();
                                const char *dl_url_cstr = dlUrlBytes.constData();
                                if (strncmp(dl_url_cstr, "http", 4) == 0)
                                    strncpy(profile_url, dl_url_cstr, sizeof(profile_url) - 1);
                                else
                                    snprintf(profile_url, sizeof(profile_url), "https://dl.liteloader.com/%s", dl_url_cstr);

                                McVersion *v = new_version();
                                if (v && mc_version_fetch(v, profile_url)) {
                                    make_loader_version_id("liteloader", mc_ver, mc_ver, v, v->raw_json);
                                    save_version_profile(v, mc_dir);
                                    del_version(v);
                                    result = 1;
                                } else {
                                    del_version(v);
                                    char output_path[MC_PATH_MAX];
                                    snprintf(output_path, sizeof(output_path), "%s/liteloader-%s.jar", mc_dir, mc_ver);
                                    if (download_file(profile_url, output_path)) {
                                        mc_info("LiteLoader JAR saved: %s", output_path);
                                        result = 1;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    char data_path[MC_PATH_MAX];
    snprintf(data_path, sizeof(data_path), "%s/liteloader-%s-data.json", mc_dir, mc_ver);
    FILE *f = fopen(data_path, "w");
    if (f) { fputs(resp->data, f); fclose(f); }
    mc_info("LiteLoader data saved to %s", data_path);
    mc_http_response_free(resp);

    if (!result) {
        char jar_url[512];
        snprintf(jar_url, sizeof(jar_url),
                 "https://dl.liteloader.com/versions/com/mumfrey/liteloader/liteloader-%s-release.jar", mc_ver);
        char output_path[MC_PATH_MAX];
        snprintf(output_path, sizeof(output_path), "%s/liteloader-%s.jar", mc_dir, mc_ver);
        if (download_file(jar_url, output_path)) {
            mc_info("LiteLoader JAR saved: %s", output_path);
            return 0;
        }
        mc_error("Failed to download LiteLoader");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    mc_console_init();
    mc_log_set_level(MC_LOG_INFO);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) { mc_output_set_mode(MC_OUTPUT_JSON); }
        if (strcmp(argv[i], "--debug") == 0) { mc_log_set_level(MC_LOG_DEBUG); }
    }

    if (mc_mirror_load_config("mirrors.json"))
        mc_info("Loaded mirror config from mirrors.json");

    const char *loader_type = nullptr;
    const char *loader_ver = nullptr;
    const char *java_path = nullptr;
    const char *output_dir = ".";
    const char *mirror_type = "bmclapi";

    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--lang") == 0) { mc_i18n_set(argv[i + 1]); break; }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--loader") == 0 && i + 1 < argc) loader_type = argv[++i];
        else if (strcmp(argv[i], "--loader-ver") == 0 && i + 1 < argc) loader_ver = argv[++i];
        else if (strcmp(argv[i], "--java") == 0 && i + 1 < argc) java_path = argv[++i];
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) output_dir = argv[++i];
        else if (strcmp(argv[i], "--mirror") == 0 && i + 1 < argc) mirror_type = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "help") == 0) { print_help(); return 0; }
    }
    if (mirror_type && strcmp(mirror_type, "auto") == 0) {
        mc_info("Probing mirrors...");
        McMirrorProbe probes[MC_MAX_MIRROR_TYPES];
        int n = mc_mirror_probe_all(probes, MC_MAX_MIRROR_TYPES);
        for (int i = 0; i < n; i++) {
            const char *status = probes[i].available ? "OK" : "DOWN";
            mc_info("  %s: %s (%.0f ms)", probes[i].mirror_type, status, probes[i].latency_ms);
        }
        mirror_type = mc_mirror_select_best(probes, n);
        mc_info("Auto-selected mirror: %s", mirror_type);
    }
    g_mirror = mirror_type;

    if (argc < 2 || argv[1][0] == '-') {
        mc_error("Usage: installer <version-id> --loader <type> --java <path>");
        print_help();
        return 1;
    }

    const char *mc_ver = argv[1];

    if (!loader_type) {
        mc_error("--loader <type> is required (forge/fabric/quilt/neoforge/optifine/liteloader)");
        return 1;
    }

    if (!java_path) {
        mc_error("--java <path> is required. Use mcjava list to find available Java runtimes.");
        return 1;
    }

    if (!mc_path_exists(java_path)) {
        mc_error("Java not found at: %s", java_path);
        return 1;
    }

    mc_info("Installing %s for Minecraft %s", loader_type, mc_ver);
    mc_info("Java: %s", java_path);
    mc_info("Output: %s", output_dir);
    if (loader_ver && *loader_ver) mc_info("Loader version: %s", loader_ver);

    if (strcmp(loader_type, "fabric") == 0)
        return install_fabric(mc_ver, loader_ver, output_dir, java_path);
    if (strcmp(loader_type, "forge") == 0)
        return install_forge(mc_ver, loader_ver, output_dir, java_path);
    if (strcmp(loader_type, "quilt") == 0)
        return install_quilt(mc_ver, loader_ver, output_dir, java_path);
    if (strcmp(loader_type, "neoforge") == 0)
        return install_neoforge(mc_ver, loader_ver, output_dir, java_path);
    if (strcmp(loader_type, "optifine") == 0)
        return install_optifine(mc_ver, output_dir, java_path);
    if (strcmp(loader_type, "liteloader") == 0)
        return install_liteloader(mc_ver, output_dir, java_path);

    mc_error("Unknown loader type: %s (supported: forge, fabric, quilt, neoforge, optifine, liteloader)", loader_type);
    return 1;
}
