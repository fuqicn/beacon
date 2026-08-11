/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_java.h"
#include <mc_str.h>
#include <mc_path.h>
#include <mc_log.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>

static int scan_path_env(std::vector<std::string> &results) {
    QString path = QProcessEnvironment::systemEnvironment().value("PATH");
    if (path.isEmpty()) return 0;
    QStringList dirs = path.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &dir : dirs) {
        QString exe = QDir(dir).absoluteFilePath(
#ifdef _WIN32
            "java.exe"
#else
            "java"
#endif
        );
        if (QFileInfo::exists(exe))
            results.push_back(exe.toUtf8().constData());
    }
    return (int)results.size();
}

static int scan_directory(const char *root, int max_depth, std::vector<std::string> &results) {
    if (!mc_path_exists(root)) return 0;
    QStringList dirs;
    dirs.append(QString::fromUtf8(root));
    int depth = 0;
    while (!dirs.isEmpty() && depth < max_depth) {
        QStringList next;
        for (const QString &d : dirs) {
            QDir dir(d);
            QString exe = dir.absoluteFilePath(
#ifdef _WIN32
                "bin/java.exe"
#else
                "bin/java"
#endif
            );
            if (QFileInfo::exists(exe))
                results.push_back(exe.toUtf8().constData());
            QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &entry : entries)
                next.append(entry.absoluteFilePath());
        }
        dirs = next;
        depth++;
    }
    return (int)results.size();
}

static int scan_where_command(std::vector<std::string> &results) {
#ifdef _WIN32
    QProcess proc;
    proc.start("where", QStringList() << "java");
    if (!proc.waitForFinished(5000)) return 0;
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
#else
    QProcess proc;
    proc.start("which", QStringList() << "java");
    if (!proc.waitForFinished(5000)) return 0;
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
#endif
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && QFileInfo::exists(trimmed))
            results.push_back(trimmed.toUtf8().constData());
    }
    return (int)results.size();
}

static int parse_java_version(const char *java_path, McJavaRuntime *rt) {
    memset(rt, 0, sizeof(*rt));
    strncpy(rt->path, java_path, sizeof(rt->path) - 1);

    char dir[MC_PATH_MAX], javac_path[MC_PATH_MAX];
    mc_path_dirname(java_path, dir, sizeof(dir));
#ifdef _WIN32
    mc_path_join(dir, "javac.exe", javac_path, sizeof(javac_path));
#else
    mc_path_join(dir, "javac", javac_path, sizeof(javac_path));
#endif
    rt->is_jdk = mc_path_exists(javac_path) ? 1 : 0;

    QProcess proc;
    proc.start(QString::fromUtf8(java_path), QStringList() << "-version");
    if (!proc.waitForFinished(5000)) return 0;
    QString output = QString::fromLocal8Bit(proc.readAllStandardError());
    if (output.isEmpty())
        output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QByteArray buf = output.toUtf8();
    const char *str = buf.constData();

    const char *ver_start = strstr(str, "version \"");
    if (ver_start) {
        ver_start += 9;
        const char *ver_end = strchr(ver_start, '"');
        if (ver_end) {
            size_t vlen = ver_end - ver_start;
            if (vlen < sizeof(rt->version)) {
                memcpy(rt->version, ver_start, vlen);
                rt->version[vlen] = '\0';
            }
            int major = 0;
            int v1 = 0, v2 = 0;
            if (sscanf(ver_start, "%d.%d", &v1, &v2) >= 1) {
                if (v1 == 1) major = v2;
                else major = v1;
            }
            rt->major_version = major;
        }
    }

    rt->is_64bit = (strstr(str, "64-Bit") != nullptr || strstr(str, "64bit") != nullptr ||
                    strstr(str, "amd64") != nullptr || strstr(str, "x64") != nullptr) ? 1 : 0;

    if (strstr(str, "Temurin") || strstr(str, "Eclipse"))
        strncpy(rt->vendor, "Eclipse Temurin", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Liberica") || strstr(str, "BellSoft"))
        strncpy(rt->vendor, "BellSoft Liberica", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Zulu") || strstr(str, "Azul"))
        strncpy(rt->vendor, "Azul Zulu", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Corretto") || strstr(str, "Amazon"))
        strncpy(rt->vendor, "Amazon Corretto", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Microsoft"))
        strncpy(rt->vendor, "Microsoft", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Semeru") || strstr(str, "IBM"))
        strncpy(rt->vendor, "IBM Semeru", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Dragonwell"))
        strncpy(rt->vendor, "Alibaba Dragonwell", sizeof(rt->vendor) - 1);
    else if (strstr(str, "GraalVM"))
        strncpy(rt->vendor, "GraalVM", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Java(TM)") || strstr(str, "Oracle"))
        strncpy(rt->vendor, "Oracle", sizeof(rt->vendor) - 1);
    else if (strstr(str, "Kona"))
        strncpy(rt->vendor, "Tencent Kona", sizeof(rt->vendor) - 1);
    else if (strstr(str, "JetBrains"))
        strncpy(rt->vendor, "JetBrains", sizeof(rt->vendor) - 1);
    else if (strstr(str, "OpenJDK"))
        strncpy(rt->vendor, "OpenJDK", sizeof(rt->vendor) - 1);
    else
        strncpy(rt->vendor, "Unknown", sizeof(rt->vendor) - 1);

    return 1;
}

int mc_java_find_all(McJavaRuntime *runtimes, int max) {
    std::vector<std::string> paths;

    scan_path_env(paths);
    scan_where_command(paths);

    {
        QString cwd = QDir::currentPath();
        scan_directory(QDir(cwd).absoluteFilePath(".runtime").toUtf8().constData(), 4, paths);
        scan_directory(QDir(cwd).absoluteFilePath("runtime").toUtf8().constData(), 4, paths);
        scan_directory(QDir(cwd).absoluteFilePath("java-runtime").toUtf8().constData(), 4, paths);
        scan_directory(cwd.toUtf8().constData(), 2, paths);
    }

#ifdef _WIN32
    const char *win_common[] = {
        "C:\\Program Files\\Java",
        "C:\\Program Files (x86)\\Java",
        "C:\\Program Files\\Eclipse Adoptium",
        "C:\\Program Files\\Eclipse Foundation",
        "C:\\Program Files\\Microsoft",
    };
    for (auto p : win_common) scan_directory(p, 4, paths);

    QString localAppData = QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        scan_directory(localAppData.toUtf8().constData(), 6, paths);
        QString msPath = QDir(localAppData).absoluteFilePath(
            "Packages\\Microsoft.4297127D64EC6_8wekyb3d8bbwe\\LocalCache\\Local\\runtime");
        scan_directory(msPath.toUtf8().constData(), 4, paths);
    }

    QString userProfile = QProcessEnvironment::systemEnvironment().value("USERPROFILE");
    if (!userProfile.isEmpty()) {
        QString mcRt = QDir(userProfile).absoluteFilePath(".minecraft\\runtime");
        scan_directory(mcRt.toUtf8().constData(), 4, paths);
    }
#elif defined(__APPLE__)
    scan_directory("/Library/Java/JavaVirtualMachines", 3, paths);
    scan_directory("/System/Library/Java/JavaVirtualMachines", 3, paths);
    scan_directory("/usr/local", 4, paths);
    scan_directory("/opt/homebrew/Cellar", 3, paths);
#else
    scan_directory("/usr/lib/jvm", 3, paths);
    scan_directory("/usr/local", 4, paths);
    scan_directory("/opt", 3, paths);
#endif

    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    int count = 0;
    for (auto &p : paths) {
        if (count >= max) break;
        McJavaRuntime rt;
        if (parse_java_version(p.c_str(), &rt))
            runtimes[count++] = rt;
    }
    return count;
}

int mc_java_find_best(McJavaRuntime *rt) {
    McJavaRuntime all[MC_JAVA_MAX];
    int count = mc_java_find_all(all, MC_JAVA_MAX);
    if (count == 0) return 0;

    int best = 0;
    for (int i = 1; i < count; i++) {
        if (all[i].major_version > all[best].major_version ||
            (all[i].major_version == all[best].major_version &&
             all[i].is_jdk && !all[best].is_jdk)) {
            best = i;
        }
    }
    *rt = all[best];
    return 1;
}
