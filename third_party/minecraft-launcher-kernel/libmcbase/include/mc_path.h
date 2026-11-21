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

// Recursively set the owner-execute bit on all .so/.dylib files under `dir`.
// This mirrors HMCL's FileUtils.setExecutable() which is called after native
// extraction so that System.loadLibrary() does not fail with "permission denied"
// on Linux and macOS.
void mc_path_set_natives_executable(const char *natives_dir);

// Remove the com.apple.quarantine xattr from all regular files under `dir`.
// Best-effort: returns 0 on success, non-zero if the operation fails
// (e.g. no xattr present, or permission denied).  Only meaningful on macOS.
int mc_remove_macos_quarantine(const char *dir);

int mc_path_appdata(char *out, size_t out_size);
int mc_path_temp(char *out, size_t out_size);
int mc_path_exe_dir(char *out, size_t out_size);
long mc_path_filesize(const char *path);

#endif
