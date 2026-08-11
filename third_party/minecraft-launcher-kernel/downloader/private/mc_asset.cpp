/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_asset.h"
#include "mc_http.h"
#include "mc_path.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "mc_str.h"
#include "mc_log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iostream>

int mc_asset_index_parse(McAssetIndex *idx, const char *json_data) {
    if (!idx || !json_data) return 0;
    memset(idx, 0, sizeof(McAssetIndex));
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_data));
    if (doc.isNull() || !doc.isObject()) return 0;
    QJsonObject obj = doc.object();
    idx->is_virtual = obj.value("virtual").toBool(false);
    idx->map_to_resources = obj.value("map_to_resources").toBool(false);
    QJsonValue objectsVal = obj.value("objects");
    if (!objectsVal.isObject()) return 0;
    QJsonObject objectsObj = objectsVal.toObject();
    int count = objectsObj.size();
    idx->objects = (McAssetObject *)calloc(count, sizeof(McAssetObject));
    if (!idx->objects) return 0;
    idx->count = 0;
    for (auto it = objectsObj.begin(); it != objectsObj.end(); ++it) {
        strncpy(idx->objects[idx->count].virtual_path, it.key().toUtf8().constData(),
                sizeof(idx->objects[idx->count].virtual_path) - 1);
        QJsonObject o = it.value().toObject();
        const QString hash = o.value("hash").toString();
        strncpy(idx->objects[idx->count].hash, hash.toUtf8().constData(),
                sizeof(idx->objects[idx->count].hash) - 1);
        idx->objects[idx->count].size = (long)o.value("size").toDouble(0);
        idx->count++;
    }
    return 1;
}

int mc_asset_index_fetch(McAssetIndex *idx, McVersion *v, const char *mc_dir) {
    if (!idx || !v || !mc_dir) return 0;
    const char *index_url = v->asset_index.url;
    const char *index_id = v->asset_index.id;
    if (!index_id || !*index_id) index_id = v->assets;
    if (!index_id || !*index_id) return 0;
    char index_path[MC_PATH_MAX];
    mc_path_join3(mc_dir, "assets", "indexes", index_path, sizeof(index_path));
    mc_path_mkdir_p(index_path);
    char index_file[MC_PATH_MAX];
    mc_path_join(index_path, index_id, index_file, sizeof(index_file));
    char full_path[MC_PATH_MAX];
    {
        std::ostringstream oss;
        oss << index_file << ".json";
        strncpy(full_path, oss.str().c_str(), sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }

    if (mc_path_exists(full_path)) {
        mc_info("Loading cached asset index: %s", index_id);
        FILE *f = fopen(full_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (len > 0) {
                char *data = (char *)malloc((size_t)len + 1);
                if (data) {
                    fread(data, 1, (size_t)len, f);
                    data[len] = '\0';
                    fclose(f);
                    int ok = mc_asset_index_parse(idx, data);
                    free(data);
                    if (ok && idx->count > 0) return 1;
                } else { fclose(f); }
            } else { fclose(f); }
        }
    }

    if (!index_url || !*index_url) {
        mc_error("No asset index URL for version %s", v->id);
        return 0;
    }

    mc_info("Fetching asset index: %s", index_url);
    McHttpClient client;
    mc_http_init(&client);
    mc_http_set_timeout(&client, 30000);
    McHttpResponse *resp = mc_http_get(&client, index_url);
    if (!resp || !resp->success || !resp->data) {
        mc_error("Failed to fetch asset index");
        if (resp) mc_http_response_free(resp);
        return 0;
    }
    int ok = mc_asset_index_parse(idx, resp->data);
    if (ok) {
        mc_path_mkdir_p(index_path);
        FILE *f = fopen(full_path, "w");
        if (f) { fputs(resp->data, f); fclose(f); }
        mc_info("Asset index loaded: %d objects", idx->count);
    }
    mc_http_response_free(resp);
    return ok;
}

void mc_asset_index_free(McAssetIndex *idx) {
    if (!idx) return;
    free(idx->objects);
    memset(idx, 0, sizeof(McAssetIndex));
}

int mc_asset_url(const char *hash, char *out, size_t out_size) {
    if (!hash || !out || out_size < 128) return 0;
    std::ostringstream oss;
    oss << "https://resources.download.minecraft.net/" << std::string(hash, 2) << "/" << hash;
    strncpy(out, oss.str().c_str(), out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
}

int mc_asset_object_path(const char *hash, const char *mc_dir, char *out, size_t out_size) {
    if (!hash || !mc_dir || !out || out_size < MC_PATH_MAX) return 0;
    char prefix[3] = { hash[0], hash[1], '\0' };
    char obj_dir[MC_PATH_MAX];
    mc_path_join3(mc_dir, "assets", "objects", obj_dir, sizeof(obj_dir));
    mc_path_mkdir_p(obj_dir);
    char pref_dir[MC_PATH_MAX];
    mc_path_join(obj_dir, prefix, pref_dir, sizeof(pref_dir));
    mc_path_mkdir_p(pref_dir);
    return mc_path_join(pref_dir, hash, out, (int)out_size);
}

int mc_asset_virtual_path(const char *hash, const char *mc_dir, const char *virtual_path,
                           int map_to_resources, char *out, size_t out_size)
{
    if (!hash || !mc_dir || !virtual_path || !out || out_size < MC_PATH_MAX) return 0;
    if (map_to_resources) {
        return mc_path_join(mc_dir, virtual_path, out, (int)out_size);
    }
    char virt_dir[MC_PATH_MAX];
    mc_path_join3(mc_dir, "assets", "virtual", virt_dir, sizeof(virt_dir));
    mc_path_mkdir_p(virt_dir);
    char legacy_dir[MC_PATH_MAX];
    mc_path_join(virt_dir, "legacy", legacy_dir, sizeof(legacy_dir));
    return mc_path_join(legacy_dir, virtual_path, out, (int)out_size);
}
