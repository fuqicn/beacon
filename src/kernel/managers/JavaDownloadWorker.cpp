/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "JavaDownloadWorker.h"

#include <mc_log.h>
#include <mc_path.h>
#include <mc_java_dl.h>
#include <mc_download.h>
#include <mc_download_qt.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QElapsedTimer>
#include <vector>
#include <cstring>

JavaDownloadWorker::JavaDownloadWorker(int majorVersion, const QString &targetDir,
                                       QObject *parent)
    : QObject(parent), m_majorVersion(majorVersion), m_targetDir(targetDir)
{
}

void JavaDownloadWorker::run()
{
    mc_info("[DL-J] Worker started: ver=%d dir=%s",
            m_majorVersion, m_targetDir.toUtf8().constData());

    QElapsedTimer timer;
    timer.start();

    McJavaFileList list;
    memset(&list, 0, sizeof(list));

    if (!mc_java_download_manifest(m_majorVersion, mc_download_effective_mirror(), &list)) {
        mc_info("[DL-J] primary mirror failed, trying bmclapi...");
        if (!mc_java_download_manifest(m_majorVersion, "bmclapi", &list)) {
            mc_info("[DL-J] bmclapi failed, trying mojang...");
            if (!mc_java_download_manifest(m_majorVersion, "mojang", &list)) {
                mc_error("[DL-J] Failed to fetch Java %d manifest", m_majorVersion);
                emit finished(false, QString(), m_majorVersion);
                return;
            }
        }
    }

    QString javaDir = m_targetDir;
    QDir().mkpath(javaDir);

    static const QSet<QByteArray> skipHashes = {
        QByteArray::fromHex("12976a6c2b227cbac58969c1455444596c894656"),
        QByteArray::fromHex("c80e4bab46e34d02826eab226a4441d0970f2aba"),
        QByteArray::fromHex("84d2102ad171863db04e7ee22a259d1f6c5de4a5")
    };

    struct FileEntry {
        QByteArray mirrorUrl;
        QByteArray originalUrl;
        QByteArray path;
        QByteArray sha1;
        long size = 0;
    };
    std::vector<FileEntry> pending;
    int completed = 0;

    for (int i = 0; i < list.count; ++i) {
        auto &f = list.files[i];

        if (skipHashes.contains(QByteArray::fromRawData(f.sha1, 40)))
            continue;

        QString fullPath = QDir(javaDir).filePath(QString::fromUtf8(f.path));
        QFileInfo fi(fullPath);
        if (fi.exists() && fi.size() == f.size) {
            completed++;
            continue;
        }

        FileEntry e;
        e.originalUrl = QByteArray(f.url);
        char mirrorBuf[2048];
        if (mc_download_translate_mojang_url_auto(f.url, mirrorBuf, sizeof(mirrorBuf))) {
            e.mirrorUrl = QByteArray(mirrorBuf);
        } else {
            e.mirrorUrl = e.originalUrl;
        }
        e.path = fullPath.toUtf8();
        e.sha1 = QByteArray(f.sha1);
        e.size = f.size;
        pending.push_back(std::move(e));
    }

    int total = static_cast<int>(pending.size()) + completed;
    emit totalFilesChanged(total);
    emit completedFilesChanged(completed);

    if (!pending.empty()) {
        for (const auto &e : pending) {
            char d[1024];
            mc_path_dirname(e.path.constData(), d, sizeof(d));
            mc_path_mkdir_p(d);
        }
    }

    int okFiles = completed;
    if (!pending.empty()) {
        const int chunkSize = 64;
        int n = static_cast<int>(pending.size());
        std::vector<int> results(n, 0);

        emit progressChanged(0.0, QStringLiteral("Downloading Java %1...").arg(m_majorVersion));

        for (int chunkStart = 0; chunkStart < n; chunkStart += chunkSize) {
            int chunkEnd = std::min(chunkStart + chunkSize, n);
            int chunkCount = chunkEnd - chunkStart;

            emit subTaskChanged(QStringLiteral("%1-%2 / %3")
                                    .arg(chunkStart + 1).arg(chunkEnd).arg(n));

            // Per-file ordered sources: [mirror, original]; a failed source
            // only falls through to the next source for that file.
            std::vector<std::vector<QByteArray>> srcBa(chunkCount);
            std::vector<std::vector<const char *>> srcLists(chunkCount);
            std::vector<McQtBatchItem> items;
            items.reserve(chunkCount);
            for (int i = chunkStart; i < chunkEnd; ++i) {
                int k = i - chunkStart;
                auto &sb = srcBa[k];
                sb.push_back(pending[i].originalUrl);
                if (pending[i].originalUrl != pending[i].mirrorUrl)
                    sb.push_back(pending[i].mirrorUrl);
                auto &sl = srcLists[k];
                for (const auto &b : sb) sl.push_back(b.constData());

                McQtBatchItem it{};
                it.urls = sl.data();
                it.url_count = (int)sl.size();
                it.path = pending[i].path.constData();
                it.sha1 = pending[i].sha1.isEmpty() ? nullptr : pending[i].sha1.constData();
                it.size = pending[i].size;
                items.push_back(it);
            }

            mc_qt_download_batch_ex(items.data(), chunkCount, 20000, &results[chunkStart]);

            for (int i = chunkStart; i < chunkEnd; ++i) {
                if (results[i])
                    okFiles++;
            }
            emit completedFilesChanged(okFiles);
            emit progressChanged(static_cast<qreal>(okFiles) / total,
                                 QStringLiteral("Downloading Java %1...").arg(m_majorVersion));
        }
    }

    mc_java_file_list_free(&list);

    bool allOk = (okFiles >= total);
    if (!allOk) {
        QDir(javaDir).removeRecursively();
        mc_info("[DL-J] Java %d download failed, cleaned up %s (%d/%d ok, %lldms)",
                m_majorVersion, javaDir.toUtf8().constData(), okFiles, total,
                (long long)timer.elapsed());
        emit finished(false, QString(), m_majorVersion);
        return;
    }

    QString javaPath;
#ifdef Q_OS_WIN
    javaPath = QDir(javaDir).filePath("bin/java.exe");
#else
    javaPath = QDir(javaDir).filePath("bin/java");
#endif

    mc_info("[DL-J] Java %d done: %d/%d files ok (%lldms)",
            m_majorVersion, okFiles, total, (long long)timer.elapsed());
    emit finished(true, javaPath, m_majorVersion);
}
