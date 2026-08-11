/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_LIBRARY_H
#define MC_LIBRARY_H

#include "mc_version.h"

// Resolve maven name to library path: libraries/group/artifact/version/artifact-version.jar
int mc_library_resolve_path(const char *name, char *out, size_t out_size);

// Resolve library URL with mirror support
int mc_library_resolve_url(const char *name, const char *mirror_base, char *out, size_t out_size);

// Resolve native library path with classifier replacement
int mc_library_natives_path(const char *name, const char *natives_key, char *out, size_t out_size);

#endif
