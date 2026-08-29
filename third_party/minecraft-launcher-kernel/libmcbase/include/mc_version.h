/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_VERSION_H
#define MC_VERSION_H

#include <QJsonObject>
#include <QJsonValue>

#define MC_MAX_LIBRARIES 1024
#define MC_MAX_ARGUMENTS 256

typedef struct {
    char name[256];
    char path[512];
    char url[512];
    char sha1[64];
    long size;
    int is_natives;
    char natives_key[64];
    char classifier_url[512];
    char classifier_sha1[64];
    long classifier_size;
    char extract_exclude[8][64];
    int exclude_count;
    int is_required;
} McLibrary;

typedef struct {
    char id[64];
    char url[512];
    char sha1[64];
    long size;
    long total_size;
} McAssetRef;

typedef struct {
    char id[64];
    char type[32];
    char main_class[256];
    char minecraft_arguments[2048];
    char inherits_from[64];
    char jar[64];
    char assets[64];
    McAssetRef asset_index;
    int java_major_version;
    char java_component[64];
    McLibrary libraries[MC_MAX_LIBRARIES];
    int library_count;
    char client_url[512];
    char client_sha1[64];
    long client_size;
    char server_url[512];
    char server_sha1[64];
    long server_size;
    char logging_client_url[512];
    char logging_client_sha1[64];
    char source_url[512];
    int is_loaded;
    QJsonObject raw_json;
} McVersion;

void mc_version_init(McVersion *v);
int mc_version_parse(McVersion *v, const char *json_data);
int mc_version_parse_file(McVersion *v, const char *path);
int mc_version_fetch(McVersion *v, const char *url);
int mc_version_fetch_by_id(McVersion *v, const char *version_id);
int mc_version_fetch_by_id_mirror(McVersion *v, const char *version_id, const char *mirror_type);
void mc_version_free(McVersion *v);

typedef int (*McRuleEvalFunc)(const char *os_name, const char *os_arch, const char *os_version);
int mc_version_evaluate_rules(const QJsonValue &rules_node);

const char *mc_platform_get(void);
void mc_platform_set(const char *platform);
const char *mc_platform_arch_get(void);
void mc_platform_arch_set(const char *arch);

// Convert current platform+arch to Eclipse Adoptium release name:
//   linux-x64     -> linux_x64
//   linux-arm64   -> linux_aarch64
//   osx-x64       -> mac_x64
//   osx-arm64     -> mac_aarch64
//   windows-x64   -> windows_x64
//   windows-arm64 -> windows_aarch64
void mc_platform_adoptium_name(char *out, size_t out_size);

// Convert our arch string to Adoptium/ASDF arch string
//   x64  -> x64
//   arm64 -> aarch64
//   x86  -> x86_32
const char *mc_arch_adoptium(const char *arch);

#endif
