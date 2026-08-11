/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_JAVA_H
#define MC_JAVA_H

#define MC_JAVA_MAX 128
#define MC_JAVA_NAME_MAX 64
#define MC_JAVA_PATH_MAX 1024

typedef struct {
    char path[MC_JAVA_PATH_MAX];
    int major_version;
    char version[32];
    char vendor[MC_JAVA_NAME_MAX];
    int is_64bit;
    int is_jdk;
} McJavaRuntime;

int mc_java_find_all(McJavaRuntime *runtimes, int max);
int mc_java_find_best(McJavaRuntime *rt);

#endif
