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
void mc_java_file_list_free(McJavaFileList *list);

#endif
