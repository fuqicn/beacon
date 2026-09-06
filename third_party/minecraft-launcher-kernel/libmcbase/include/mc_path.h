/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_PATH_H
#define MC_PATH_H

#include <stddef.h>

#define MC_PATH_MAX 1024

int mc_path_join(const char *a, const char *b, char *out, size_t out_size);
int mc_path_join3(const char *a, const char *b, const char *c, char *out, size_t out_size);
int mc_path_join4(const char *a, const char *b, const char *c, const char *d, char *out, size_t out_size);
const char *mc_path_filename(const char *path);
const char *mc_path_extension(const char *path);
int mc_path_dirname(const char *path, char *out, size_t out_size);
int mc_path_normalize(const char *path, char *out, size_t out_size);
int mc_path_exists(const char *path);
int mc_path_isdir(const char *path);
int mc_path_mkdir(const char *path);
int mc_path_mkdir_p(const char *path);
int mc_path_rmdir_recursive(const char *path);
int mc_path_appdata(char *out, size_t out_size);
int mc_path_temp(char *out, size_t out_size);
int mc_path_exe_dir(char *out, size_t out_size);
long mc_path_filesize(const char *path);

#endif
