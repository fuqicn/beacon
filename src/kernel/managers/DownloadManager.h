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
#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QThread>
#include <QPointer>
#include <QStringList>
#include <QElapsedTimer>

class DownloadWorker;

class DownloadManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString currentTask READ currentTask NOTIFY progressChanged)
    Q_PROPERTY(QString subTask READ subTask NOTIFY subTaskChanged)
    Q_PROPERTY(int totalFiles READ totalFiles NOTIFY totalFilesChanged)
    Q_PROPERTY(int completedFiles READ completedFiles NOTIFY completedFilesChanged)
    Q_PROPERTY(double speedBytes READ speedBytes NOTIFY speedBytesChanged)

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    bool busy() const { return m_busy; }
    qreal progress() const { return m_progress; }
    QString currentTask() const { return m_currentTask; }
    QString subTask() const { return m_subTask; }
    int totalFiles() const { return m_totalFiles; }
    int completedFiles() const { return m_completedFiles; }
    double speedBytes() const { return m_speedBytes; }
    QString lastDownloadDir() const { return m_lastDownloadDir; }

    Q_INVOKABLE void startDownload(const QString &versionId, const QString &dir);
    Q_INVOKABLE void downloadAll(const QString &versionId, const QString &dir,
                                 const QString &mirror = "bmclapi");
    Q_INVOKABLE void downloadClient(const QString &versionId, const QString &dir,
                                    const QString &mirror = "bmclapi");
    Q_INVOKABLE void downloadJava(int majorVersion, const QString &dir);
    Q_INVOKABLE void cancelAll();

signals:
    void progressChanged(qreal progress, const QString &currentFile);
    void subTaskChanged();
    void totalFilesChanged();
    void completedFilesChanged();
    void speedBytesChanged();
    void allCompleted(bool success);
    void busyChanged();
    void errorOccurred(const QString &message);
    void javaDownloaded(int majorVersion, const QString &javaPath);

private slots:
    void onWorkerProgress(qreal p, const QString &task);
    void onWorkerSubTask(const QString &t);
    void onWorkerTotalFiles(int total);
    void onWorkerCompletedFiles(int completed);
    void onWorkerSpeed(double speed);
    void onWorkerFinished(bool success, const QString &error);

private:
    void startWorker(DownloadWorker *worker);

    bool m_busy = false;
    qreal m_progress = 0.0;
    QString m_currentTask;
    QString m_subTask;
    int m_totalFiles = 0;
    int m_completedFiles = 0;
    double m_speedBytes = 0.0;
    QString m_lastDownloadDir;

    QThread *m_workerThread = nullptr;
    QPointer<DownloadWorker> m_activeWorker;
    // True when *this* manager requested a global download cancellation.
    // Used to pair mc_qt_download_set_cancel(true) with a matching false only
    // after our worker actually finishes, so we never clear another module's
    // in-flight cancellation (shared global cancel flag).
    bool m_cancelPending = false;
};

#endif
