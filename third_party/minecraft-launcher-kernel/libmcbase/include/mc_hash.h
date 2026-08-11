/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_HASH_H
#define MC_HASH_H

#include <stddef.h>

int mc_hash_file_sha1(const char *path, char *hex_out, size_t hex_size);
int mc_hash_file_sha256(const char *path, char *hex_out, size_t hex_size);
int mc_hash_file_sha512(const char *path, char *hex_out, size_t hex_size);
int mc_hash_data_sha1(const unsigned char *data, size_t len, char *hex_out, size_t hex_size);
int mc_hash_data_sha256(const unsigned char *data, size_t len, char *hex_out, size_t hex_size);
int mc_hash_data_md5(const unsigned char *data, size_t len, unsigned char *raw_out);

#endif
