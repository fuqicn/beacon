/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_JAVA_DL_H
#define MC_JAVA_DL_H

typedef struct {
    char url[2048];
    char path[1024];
    char sha1[64];
    long size;
} McJavaFile;

typedef struct {
    McJavaFile *files;
    int count;
    int capacity;
} McJavaFileList;

int mc_java_download_manifest(int major_version, const char *mirror, McJavaFileList *list);
// Legacy concurrent manifest fetch: spawns a bare std::thread per source,
// each running a nested QEventLoop::exec() over a thread-local
// QNetworkAccessManager. This is the historical UI-freeze root cause, kept
// verbatim so the manual memory-optimization tool can reproduce the exact
// stall (called on a worker thread, cancelled as soon as the file count is
// known). Not used by the normal download path.
int mc_java_download_manifest_legacy(int major_version, const char *mirror, McJavaFileList *list);
void mc_java_file_list_free(McJavaFileList *list);

#endif
