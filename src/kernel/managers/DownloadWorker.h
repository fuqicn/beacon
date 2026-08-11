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
#ifndef DOWNLOADWORKER_H
#define DOWNLOADWORKER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <atomic>
#include <mc_version.h>
#include <mc_asset.h>

class DownloadWorker : public QObject
{
    Q_OBJECT
public:
    DownloadWorker(const QString &versionId, const QString &dir);
    ~DownloadWorker() override;

    void cancel() { m_cancelled = true; }

public slots:
    void run();

signals:
    void progressChanged(qreal progress, const QString &currentFile);
    void subTaskChanged(const QString &subTask);
    void totalFilesChanged(int total);
    void completedFilesChanged(int completed);
    void speedBytesChanged(double speed);
    void finished(bool success, const QString &error);

private:
    void setProgress(qreal p, const QString &task);
    void setSubTask(const QString &t);
    void doStartDownload();
    void retryOrFinish(bool ok);
    void finish(bool success);

    void stepFetchVersion();
    void stepDownloadAll();
    void stepDownloadLogging();

    void translateUrl(const char *origUrl, char *out, size_t outSize);
    int batchDownloadWithRetry(QStringList &urls, QStringList &paths,
                               QStringList &sha1s, QList<long> &sizes,
                               int timeoutMs = 60000,
                               QStringList *fallbackUrls = nullptr);
    void emitProgress();
    void updateSpeed();

    QString m_versionId;
    QString m_dir;

    bool m_busy = false;
    qreal m_progress = 0.0;
    QString m_currentTask;
    QString m_subTask;
    int m_totalFiles = 0;
    int m_completedFiles = 0;
    double m_speedBytes = 0.0;
    QElapsedTimer m_speedTimer;
    qint64 m_lastBytes = 0;
    qint64 m_bytesDone = 0;

    int m_attemptCount = 0;
    static const int MaxAttempts = 3;
    QElapsedTimer m_lastEmit;

    McVersion m_ver{};
    bool m_verOwned = false;
    char m_mcDir[1024]{};
    char m_verDir[1024]{};
    char m_verJson[1024]{};
    char m_verJar[1024]{};
    QString m_mirror;

    std::atomic<bool> m_cancelled{false};
};

#endif
