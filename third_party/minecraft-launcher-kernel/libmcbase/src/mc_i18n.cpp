/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_i18n.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include "mc_path.h"
#include <cstring>
#include <cstdio>

static char g_lang[16] = "";
static QJsonObject g_table;
static int g_loaded = 0;

static const char *detect_system_lang(void) {
    static char buf[16] = {0};
    if (buf[0]) return buf;
    // Cross-platform language detection (Windows locale / macOS / Linux).
    QString lang = QLocale::system().bcp47Name().section('-', 0, 0).toLower();
    if (lang.isEmpty()) lang = QStringLiteral("en");
    qstrncpy(buf, lang.toUtf8().constData(), sizeof(buf));
    if (!buf[0]) strcpy(buf, "en");
    return buf;
}

static int has_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static QJsonObject parse_json_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return QJsonObject();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    QByteArray data;
    if (sz > 0) {
        data.resize((int)sz);
        fread(data.data(), 1, sz, f);
    }
    fclose(f);
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.isObject() ? doc.object() : QJsonObject();
}

static const char *lang_file_path(const char *lang) {
    static char path[1024];
    char exe_dir[1024];
    if (mc_path_exe_dir(exe_dir, sizeof(exe_dir)) && exe_dir[0]) {
        snprintf(path, sizeof(path), "%s/lang/%s.json", exe_dir, lang);
        if (has_file(path)) return path;
    }
    snprintf(path, sizeof(path), "lang/%s.json", lang);
    return path;
}

void mc_i18n_set(const char *lang) {
    g_table = QJsonObject();
    g_loaded = 0;
    if (lang) {
        strncpy(g_lang, lang, sizeof(g_lang) - 1);
        g_lang[sizeof(g_lang) - 1] = '\0';
    }
}

const char *mc_i18n(const char *key) {
    if (!key) return "";

    if (!g_loaded) {
        const char *lang = g_lang[0] ? g_lang : detect_system_lang();
        const char *path = lang_file_path(lang);
        g_table = parse_json_file(path);
        if (g_table.isEmpty()) {
            path = lang_file_path("en");
            g_table = parse_json_file(path);
        }
        g_loaded = 1;
    }

    if (!g_table.isEmpty()) {
        QJsonValue val = g_table.value(QString::fromUtf8(key));
        if (val.isString()) {
            static QByteArray buf;
            buf = val.toString().toUtf8();
            return buf.constData();
        }
    }
    return key;
}
