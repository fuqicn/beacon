/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_hash.h"
#include <cstring>

#include <QtCore/QFile>
#include <QtCore/QCryptographicHash>

static int hash_file(const char *path, char *hex_out, size_t hex_size,
                     QCryptographicHash::Algorithm alg, int raw_len) {
    if (!path || !hex_out || hex_size < (size_t)(raw_len * 2 + 1)) return 0;
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QCryptographicHash hash(alg);
    if (!hash.addData(&f)) { f.close(); return 0; }
    f.close();
    QByteArray result = hash.result();
    QByteArray hex = result.toHex();
    size_t copy_len = (size_t)hex.size();
    if (copy_len >= hex_size) copy_len = hex_size - 1;
    memcpy(hex_out, hex.constData(), copy_len);
    hex_out[copy_len] = '\0';
    return 1;
}

static int hash_data(const unsigned char *data, size_t len, char *hex_out, size_t hex_size,
                     QCryptographicHash::Algorithm alg, int raw_len) {
    if (!data || !hex_out || hex_size < (size_t)(raw_len * 2 + 1)) return 0;
    QByteArray result = QCryptographicHash::hash(
        QByteArray((const char *)data, (int)len), alg);
    QByteArray hex = result.toHex();
    size_t copy_len = (size_t)hex.size();
    if (copy_len >= hex_size) copy_len = hex_size - 1;
    memcpy(hex_out, hex.constData(), copy_len);
    hex_out[copy_len] = '\0';
    return 1;
}

int mc_hash_file_sha1(const char *path, char *hex_out, size_t hex_size) {
    return hash_file(path, hex_out, hex_size, QCryptographicHash::Sha1, 20);
}

int mc_hash_file_sha256(const char *path, char *hex_out, size_t hex_size) {
    return hash_file(path, hex_out, hex_size, QCryptographicHash::Sha256, 32);
}

int mc_hash_file_sha512(const char *path, char *hex_out, size_t hex_size) {
    return hash_file(path, hex_out, hex_size, QCryptographicHash::Sha512, 64);
}

int mc_hash_data_sha1(const unsigned char *data, size_t len, char *hex_out, size_t hex_size) {
    return hash_data(data, len, hex_out, hex_size, QCryptographicHash::Sha1, 20);
}

int mc_hash_data_sha256(const unsigned char *data, size_t len, char *hex_out, size_t hex_size) {
    return hash_data(data, len, hex_out, hex_size, QCryptographicHash::Sha256, 32);
}

int mc_hash_data_md5(const unsigned char *data, size_t len, unsigned char *raw_out) {
    if (!data || !raw_out) return 0;
    QByteArray result = QCryptographicHash::hash(
        QByteArray((const char *)data, (int)len), QCryptographicHash::Md5);
    if (result.size() != 16) return 0;
    memcpy(raw_out, result.constData(), 16);
    return 1;
}
