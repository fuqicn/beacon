/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_zip.h"
#include "mc_path.h"
#include "mc_log.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <QtZlib/zlib.h>

#define ZIP_LOCAL_SIG      0x04034b50
#define ZIP_CENTRAL_SIG    0x02014b50
#define ZIP_EOCD_SIG       0x06054b50
#define ZIP_COMPRESS_STORED   0
#define ZIP_COMPRESS_DEFLATED 8

#pragma pack(push, 1)
struct LocalFileHeader {
    uint32_t sig;
    uint16_t version;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
};

struct CentralDirEntry {
    uint32_t sig;
    uint16_t version_made;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_offset;
};

struct EOCD {
    uint32_t sig;
    uint16_t disk_num;
    uint16_t disk_start;
    uint16_t entries_disk;
    uint16_t entries_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
};
#pragma pack(pop)

static int read_eocd(FILE *f, EOCD *eocd) {
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 22) return 0;
    long start = (size - 22 > 65535) ? (size - 22 - 65535) : 0;
    for (long pos = size - 22; pos >= start; pos--) {
        fseek(f, pos, SEEK_SET);
        uint32_t sig;
        if (fread(&sig, 4, 1, f) != 1) break;
        if (sig == ZIP_EOCD_SIG) {
            fseek(f, pos, SEEK_SET);
            if (fread(eocd, sizeof(EOCD), 1, f) == 1)
                return 1;
        }
    }
    return 0;
}

int mc_zip_extract(const char *archive_path, const char *output_dir) {
    FILE *f = fopen(archive_path, "rb");
    if (!f) { mc_error("Cannot open archive: %s", archive_path); return 0; }

    EOCD eocd;
    if (!read_eocd(f, &eocd)) {
        mc_error("Invalid ZIP archive (no EOCD): %s", archive_path);
        fclose(f); return 0;
    }

    unsigned char *buf = (unsigned char *)malloc(65536);
    if (!buf) { fclose(f); return 0; }
    const size_t buf_size = 65536;
    int total = 0, ok = 0;

    uint32_t cd_pos = eocd.cd_offset;
    for (uint16_t i = 0; i < eocd.entries_total; i++) {
        CentralDirEntry cd;
        fseek(f, cd_pos, SEEK_SET);
        if (fread(&cd, sizeof(CentralDirEntry), 1, f) != 1) break;
        if (cd.sig != ZIP_CENTRAL_SIG) break;

        // Read entry name
        char name[1024];
        int nlen = cd.name_len < 1023 ? cd.name_len : 1023;
        fread(name, 1, nlen, f);
        name[nlen] = '\0';
        fseek(f, cd_pos + sizeof(CentralDirEntry) + cd.name_len + cd.extra_len + cd.comment_len, SEEK_SET);
        cd_pos = ftell(f);

        // Skip directories
        size_t nlen_str = strlen(name);
        if (nlen_str == 0 || name[nlen_str - 1] == '/')
            continue;

        char out_path[MC_PATH_MAX];
        mc_path_join(output_dir, name, out_path, sizeof(out_path));

        char dir[MC_PATH_MAX];
        mc_path_dirname(out_path, dir, sizeof(dir));
        mc_path_mkdir_p(dir);

        // Read local file header
        fseek(f, cd.local_offset, SEEK_SET);
        LocalFileHeader local;
        if (fread(&local, sizeof(LocalFileHeader), 1, f) != 1 || local.sig != ZIP_LOCAL_SIG) {
            mc_error("Invalid local header for %s", name);
            continue;
        }

        uint32_t data_offset = cd.local_offset + sizeof(LocalFileHeader) + local.name_len + local.extra_len;
        uint32_t comp_size = cd.comp_size;

        // Handle data descriptor (bit 3 of flags)
        if (cd.flags & 0x08) {
            comp_size = local.comp_size;
        }

        total++;

        FILE *out = fopen(out_path, "wb");
        if (!out) {
            mc_error("Cannot create %s", out_path);
            continue;
        }

        int entry_ok = 0;
        if (cd.method == ZIP_COMPRESS_STORED) {
            fseek(f, data_offset, SEEK_SET);
            size_t remaining = comp_size;
            while (remaining > 0) {
                size_t chunk = remaining > buf_size ? buf_size : remaining;
                size_t n = fread(buf, 1, chunk, f);
                if (n == 0) break;
                fwrite(buf, 1, n, out);
                remaining -= n;
            }
            entry_ok = (remaining == 0);
        } else if (cd.method == ZIP_COMPRESS_DEFLATED) {
            fseek(f, data_offset, SEEK_SET);
            z_stream strm;
            unsigned char *in = (unsigned char *)malloc(8192);
            unsigned char *out_buf = (unsigned char *)malloc(8192);
            if (!in || !out_buf) {
                free(in); free(out_buf);
                fclose(out); fclose(f); free(buf);
                return 0;
            }
            memset(&strm, 0, sizeof(strm));
            if (inflateInit2(&strm, -MAX_WBITS) == Z_OK) {
                size_t remaining = comp_size;
                int done = 0;
                while (!done) {
                    size_t to_read = remaining > 8192 ? 8192 : remaining;
                    size_t n = fread(in, 1, to_read, f);
                    if (n == 0 && remaining > 0) break;
                    strm.avail_in = static_cast<uInt>(n);
                    strm.next_in = in;
                    remaining -= n;
                    int err = 0;
                    do {
                        strm.avail_out = 8192;
                        strm.next_out = out_buf;
                        int ret = inflate(&strm, Z_NO_FLUSH);
                        if (ret == Z_STREAM_END) done = 1;
                        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                            err = 1;
                            done = 1;
                            break;
                        }
                        size_t have = 8192 - strm.avail_out;
                        fwrite(out_buf, 1, have, out);
                    } while (strm.avail_out == 0);
                    if (err) done = 0;
                }
                inflateEnd(&strm);
                entry_ok = done;
            }
            free(in);
            free(out_buf);
        } else {
            mc_error("Unsupported compression method %d for %s", cd.method, name);
        }

        fclose(out);
        if (entry_ok) ok++;
        else mc_error("Failed to extract %s", name);
    }

    fclose(f);
    free(buf);
    return ok == total && total > 0;
}
