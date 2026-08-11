/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_STR_H
#define MC_STR_H

#include <stddef.h>

char *mc_strdup(const char *s);
char *mc_strndup(const char *s, size_t n);
char *mc_strtrim(char *s);
char *mc_strtrim_copy(const char *s);
int mc_strsplit(const char *s, char delim, char ***out);
void mc_strsplit_free(char **arr, int count);
char *mc_strjoin(const char **parts, int count, const char *sep);
char *mc_strreplace(const char *s, const char *from, const char *to);
int mc_strstartswith(const char *s, const char *prefix);
int mc_strendswith(const char *s, const char *suffix);
int mc_strcontains(const char *s, const char *sub);
char *mc_strlower(char *s);
char *mc_strlower_copy(const char *s);
int mc_stricmp(const char *a, const char *b);
int mc_strnicmp(const char *a, const char *b, size_t n);
long mc_strtol(const char *s, int default_val);
int mc_strwildcard(const char *s, const char *pattern);

#endif
