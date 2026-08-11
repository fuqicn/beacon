/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_path.h"
#include "mc_str.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cerrno>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QCoreApplication>
#include <QtCore/QStandardPaths>

int mc_path_join(const char *a, const char *b, char *out, size_t out_size) {
    if (!a || !b || !out || out_size == 0) return 0;
    QString path = QDir(QString::fromUtf8(a)).absoluteFilePath(QString::fromUtf8(b));
    QByteArray ba = QDir::toNativeSeparators(path).toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

int mc_path_join3(const char *a, const char *b, const char *c, char *out, size_t out_size) {
    if (!a || !b || !c || !out || out_size == 0) return 0;
    QString p = QDir(QString::fromUtf8(a)).absoluteFilePath(QString::fromUtf8(b));
    p = QDir(p).absoluteFilePath(QString::fromUtf8(c));
    QByteArray ba = QDir::toNativeSeparators(p).toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

int mc_path_join4(const char *a, const char *b, const char *c, const char *d, char *out, size_t out_size) {
    if (!a || !b || !c || !d || !out || out_size == 0) return 0;
    QString p = QDir(QString::fromUtf8(a)).absoluteFilePath(QString::fromUtf8(b));
    p = QDir(p).absoluteFilePath(QString::fromUtf8(c));
    p = QDir(p).absoluteFilePath(QString::fromUtf8(d));
    QByteArray ba = QDir::toNativeSeparators(p).toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

const char *mc_path_filename(const char *path) {
    if (!path) return NULL;
    const char *p = strrchr(path, '\\');
    const char *p2 = strrchr(path, '/');
    if (p2 > p) p = p2;
    return p ? p + 1 : path;
}

const char *mc_path_extension(const char *path) {
    const char *name = mc_path_filename(path);
    if (!name) return NULL;
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

int mc_path_dirname(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return 0;
    QFileInfo fi(QString::fromUtf8(path));
    QString dir = fi.absolutePath();
    QByteArray ba = dir.toUtf8();
    if ((size_t)ba.size() >= out_size) {
        // Fallback: manual dirname
        size_t len = strlen(path);
        if (len == 0) { out[0] = '\0'; return 1; }
        const char *p = path + len - 1;
        while (p > path && (*p == '\\' || *p == '/')) p--;
        while (p > path && *(p - 1) != '\\' && *(p - 1) != '/') p--;
        if (p == path) {
            if (len >= out_size) return 0;
            memcpy(out, path, len + 1);
            return 1;
        }
        size_t dirlen = p - path;
        if (dirlen >= out_size) return 0;
        memcpy(out, path, dirlen);
        out[dirlen] = '\0';
        return 1;
    }
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

int mc_path_normalize(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return 0;
    QFileInfo fi(QString::fromUtf8(path));
    QString abs = fi.absoluteFilePath();
    QByteArray ba = QDir::toNativeSeparators(abs).toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

int mc_path_exists(const char *path) {
    if (!path) return 0;
    return QFileInfo::exists(QString::fromUtf8(path));
}

int mc_path_isdir(const char *path) {
    if (!path) return 0;
    QFileInfo fi(QString::fromUtf8(path));
    return fi.exists() && fi.isDir();
}

int mc_path_mkdir(const char *path) {
    if (!path) return 0;
    if (mc_path_exists(path)) return 1;
    QDir dir;
    return dir.mkdir(QString::fromUtf8(path)) ? 1 : 0;
}

int mc_path_mkdir_p(const char *path) {
    if (!path) return 0;
    if (mc_path_exists(path)) return 1;
    QDir dir;
    return dir.mkpath(QString::fromUtf8(path)) ? 1 : 0;
}

int mc_path_rmdir_recursive(const char *path) {
    if (!path || !mc_path_exists(path)) return 1;
    QDir dir(QString::fromUtf8(path));
    if (dir.removeRecursively()) return 1;
    // fallback: manual recursive delete
    QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        QString full = entry.absoluteFilePath();
        if (entry.isDir())
            mc_path_rmdir_recursive(full.toUtf8().constData());
        else
            QFile::remove(full);
    }
    QDir parent = dir;
    parent.cdUp();
    return parent.rmdir(dir.dirName()) ? 1 : 0;
}

int mc_path_appdata(char *out, size_t out_size) {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // On Windows: %APPDATA%/mclauncher
    // On Linux:   ~/.local/share/mclauncher
    // On macOS:   ~/Library/Application Support/mclauncher
    // QStandardPaths already returns the app-specific directory, so use parent
    QDir dir(dataDir);
    dir.cdUp();
    QString path = QDir::toNativeSeparators(dir.absolutePath());
    QByteArray ba = path.toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

int mc_path_temp(char *out, size_t out_size) {
    QString tmp = QDir::tempPath();
    QByteArray ba = tmp.toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

int mc_path_exe_dir(char *out, size_t out_size) {
    if (QCoreApplication::instance()) {
        QString dir = QCoreApplication::applicationDirPath();
        QByteArray ba = QDir::toNativeSeparators(dir).toUtf8();
        if ((size_t)ba.size() >= out_size) return 0;
        memcpy(out, ba.constData(), (size_t)ba.size() + 1);
        return 1;
    }
    // Fallback if QCoreApplication not yet created
    QFileInfo fi(QString::fromUtf8("."));
    QString abs = fi.absoluteFilePath();
    QByteArray ba = QDir::toNativeSeparators(abs).toUtf8();
    if ((size_t)ba.size() >= out_size) return 0;
    memcpy(out, ba.constData(), (size_t)ba.size() + 1);
    return 1;
}

long mc_path_filesize(const char *path) {
    if (!path) return -1;
    QFileInfo fi(QString::fromUtf8(path));
    if (!fi.exists()) return -1;
    return fi.size();
}
