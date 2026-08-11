/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_java_dl.h"
#include <mc_http.h>
#include <mc_log.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <mc_str.h>
#include <mc_download.h>
#include <mc_version.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

#include <QtCore/QDir>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>

static const char *java_manifest_url(const char *mirror) {
    if (mirror && strcmp(mirror, "bmclapi") == 0)
        return "https://bmclapi2.bangbang93.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
    return "https://piston-meta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
}

int mc_java_download_manifest(int major_version, const char *mirror, McJavaFileList *list) {
    memset(list, 0, sizeof(*list));
    const char *url = java_manifest_url(mirror);

    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch Java runtime manifest");
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(resp->data), &parseErr);
    mc_http_response_free(resp);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        mc_error("Failed to parse Java manifest JSON");
        return 0;
    }
    QJsonObject root = doc.object();

    const char *version_key = "jre-legacy";
    if (major_version >= 22) version_key = "java-runtime-epsilon";
    else if (major_version >= 21) version_key = "java-runtime-delta";
    else if (major_version >= 17) version_key = "java-runtime-beta";
    else if (major_version >= 16) version_key = "java-runtime-alpha";

    const char *plat = mc_platform_get();
    const char *arch = mc_platform_arch_get();
    char plat_key[32];
    snprintf(plat_key, sizeof(plat_key), "%s-%s", plat, arch);
    QJsonValue platVal = root.value(plat_key);
    if (platVal.isUndefined()) {
        snprintf(plat_key, sizeof(plat_key), "%s-x64", plat);
        platVal = root.value(plat_key);
    }
    if (platVal.isUndefined()) {
        mc_error("No '%s' entry in Java manifest (tried: %s-x64, %s-%s)", plat, plat, plat, arch);
        return 0;
    }

    QJsonValue rtVal = platVal.toObject().value(version_key);
    if (rtVal.isUndefined() || !rtVal.isArray()) {
        if (platVal.isObject()) {
            QJsonObject platObj = platVal.toObject();
            for (auto it = platObj.begin(); it != platObj.end(); ++it) {
                rtVal = it.value();
                version_key = it.key().toUtf8().constData();
                break;
            }
        }
    }
    if (rtVal.isUndefined() || !rtVal.isArray()) {
        mc_error("No Java runtime '%s' found in manifest", version_key);
        return 0;
    }
    QJsonArray rtArr = rtVal.toArray();

    QJsonValue rtEntryVal = rtArr.at(0);
    if (rtEntryVal.isUndefined()) {
        mc_error("Empty Java runtime array");
        return 0;
    }
    QJsonObject rtEntryObj = rtEntryVal.toObject();

    QJsonValue manifestVal = rtEntryObj.value("manifest");
    if (manifestVal.isUndefined()) {
        mc_error("No manifest in Java runtime entry");
        return 0;
    }
    QJsonObject manifestObj = manifestVal.toObject();
    QString manifestUrl = manifestObj.value("url").toString();
    if (manifestUrl.isNull()) {
        mc_error("No manifest URL in Java runtime entry");
        return 0;
    }

    char manifest_url_translated[2048];
    QByteArray manifestUrlUtf8 = manifestUrl.toUtf8();
    strncpy(manifest_url_translated, manifestUrlUtf8.constData(), sizeof(manifest_url_translated) - 1);
    manifest_url_translated[sizeof(manifest_url_translated) - 1] = '\0';
    if (mirror && strcmp(mirror, "mojang") != 0) {
        char translated[2048];
        if (mc_download_translate_mojang_url(manifestUrlUtf8.constData(), translated, sizeof(translated), mirror))
            strncpy(manifest_url_translated, translated, sizeof(manifest_url_translated) - 1);
    }
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    resp = mc_http_get(&client, manifest_url_translated);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch Java runtime file manifest");
        if (resp) mc_http_response_free(resp);
        return 0;
    }

    QJsonParseError fileParseErr;
    QJsonDocument fileDoc = QJsonDocument::fromJson(QByteArray(resp->data), &fileParseErr);
    mc_http_response_free(resp);
    if (fileParseErr.error != QJsonParseError::NoError || !fileDoc.isObject()) {
        mc_error("Failed to parse file manifest JSON");
        return 0;
    }
    QJsonObject fileRoot = fileDoc.object();

    QJsonValue filesVal = fileRoot.value("files");
    if (filesVal.isUndefined() || !filesVal.isObject()) {
        mc_error("No files in Java runtime manifest");
        return 0;
    }
    QJsonObject filesObj = filesVal.toObject();

    std::vector<McJavaFile> file_list;
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        QByteArray relPathBa = it.key().toUtf8();
        const char *rel_path = relPathBa.constData();

        QJsonValue entryVal = it.value();
        if (!entryVal.isObject()) continue;

        QJsonObject entryObj = entryVal.toObject();
        QJsonValue downloadsVal = entryObj.value("downloads");
        if (downloadsVal.isUndefined()) continue;
        QJsonValue rawVal = downloadsVal.toObject().value("raw");
        if (rawVal.isUndefined()) continue;

        QJsonObject rawObj = rawVal.toObject();
        QString dlUrl = rawObj.value("url").toString();
        QString dlSha1 = rawObj.value("sha1").toString();
        long dlSize = (long)rawObj.value("size").toDouble(0);

        if (dlUrl.isNull() || dlSha1.isNull()) continue;

        QByteArray dlUrlBa = dlUrl.toUtf8();
        QByteArray dlSha1Ba = dlSha1.toUtf8();

        McJavaFile f;
        memset(&f, 0, sizeof(f));
        strncpy(f.url, dlUrlBa.constData(), sizeof(f.url) - 1);
        strncpy(f.path, rel_path, sizeof(f.path) - 1);
        strncpy(f.sha1, dlSha1Ba.constData(), sizeof(f.sha1) - 1);
        f.size = dlSize;
        file_list.push_back(f);
    }

    if (file_list.empty()) { mc_error("No downloadable files in Java manifest"); return 0; }

    list->count = (int)file_list.size();
    list->capacity = list->count;
    list->files = (McJavaFile *)malloc(sizeof(McJavaFile) * list->count);
    if (!list->files) { list->count = 0; return 0; }
    for (int i = 0; i < list->count; i++)
        list->files[i] = file_list[i];

    mc_info("Java manifest: %d files for %s", list->count, version_key);
    return 1;
}

void mc_java_file_list_free(McJavaFileList *list) {
    if (list && list->files) {
        free(list->files);
        list->files = nullptr;
    }
    list->count = 0;
    list->capacity = 0;
}
