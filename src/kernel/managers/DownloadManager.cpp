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
#include "DownloadManager.h"
#include "DownloadWorker.h"
#include "JavaDownloadWorker.h"
#include <mc_log.h>
#include <mc_java_dl.h>
#include <mc_path.h>
#include <mc_download.h>
#include <mc_download_qt.h>

#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QCoreApplication>
#include <QUrl>
#include <cstdlib>

static QString localizeDir(const QString &dir)
{
    QString d = dir.trimmed();
    if (d.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        QUrl u(d);
        if (u.isLocalFile()) {
            QString local = u.toLocalFile();
            if (!local.isEmpty()) return local;
        }
    }
    return d;
}

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {}
DownloadManager::~DownloadManager()
{
    if (m_workerThread) {
        if (m_activeWorker) m_activeWorker->cancel();
        mc_qt_download_set_cancel(true);
        m_workerThread->quit();
        if (!m_workerThread->wait(5000)) {
            m_workerThread->terminate();
            m_workerThread->wait(3000);
        }
        mc_qt_download_set_cancel(false);
    }
}

void DownloadManager::startDownload(const QString &versionId, const QString &dir)
{
    if (m_busy) return;
    const QString localDir = localizeDir(dir);
    m_lastDownloadDir = localDir;
    if (m_cancelPending) {
        mc_qt_download_set_cancel(false);
        m_cancelPending = false;
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_busy = true;
    m_progress = 0.0;
    m_currentTask.clear();
    m_subTask.clear();
    m_totalFiles = 0;
    m_completedFiles = 0;
    m_speedBytes = 0.0;
    emit busyChanged();
    emit progressChanged(0.0, QString());
    emit totalFilesChanged();
    emit completedFilesChanged();
    emit speedBytesChanged();

    auto *worker = new DownloadWorker(versionId, localDir);
    startWorker(worker);
}

void DownloadManager::startWorker(DownloadWorker *worker)
{
    m_activeWorker = worker;

    connect(worker, &DownloadWorker::progressChanged, this, &DownloadManager::onWorkerProgress);
    connect(worker, &DownloadWorker::subTaskChanged, this, &DownloadManager::onWorkerSubTask);
    connect(worker, &DownloadWorker::totalFilesChanged, this, &DownloadManager::onWorkerTotalFiles);
    connect(worker, &DownloadWorker::completedFilesChanged, this, &DownloadManager::onWorkerCompletedFiles);
    connect(worker, &DownloadWorker::speedBytesChanged, this, &DownloadManager::onWorkerSpeed);
    connect(worker, &DownloadWorker::finished, this, &DownloadManager::onWorkerFinished);

    m_workerThread = new QThread(this);
    worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, worker, &DownloadWorker::run);
    connect(worker, &DownloadWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_activeWorker = nullptr;
    });

    m_workerThread->start();
}

void DownloadManager::onWorkerProgress(qreal p, const QString &task)
{
    m_progress = p;
    m_currentTask = task;
    emit progressChanged(p, task);
}

void DownloadManager::onWorkerSubTask(const QString &t)
{
    m_subTask = t;
    emit subTaskChanged();
}

void DownloadManager::onWorkerTotalFiles(int total)
{
    m_totalFiles = total;
    emit totalFilesChanged();
}

void DownloadManager::onWorkerCompletedFiles(int completed)
{
    m_completedFiles = completed;
    emit completedFilesChanged();
}

void DownloadManager::onWorkerSpeed(double speed)
{
    m_speedBytes = speed;
    emit speedBytesChanged();
}

void DownloadManager::onWorkerFinished(bool success, const QString &error)
{
    m_busy = false;
    emit busyChanged();
    if (!success && !error.isEmpty()) {
        mc_error("[DL] %s", error.toUtf8().constData());
        emit errorOccurred(error);
    }
    emit allCompleted(success);
    if (m_cancelPending) {
        mc_qt_download_set_cancel(false);
        m_cancelPending = false;
    }
}

void DownloadManager::downloadAll(const QString &versionId, const QString &dir,
                                  const QString &mirror)
{
    startDownload(versionId, dir);
}

void DownloadManager::downloadClient(const QString &versionId, const QString &dir,
                                     const QString &mirror)
{
    startDownload(versionId, dir);
}

void DownloadManager::downloadJava(int majorVersion, const QString &dir)
{
    if (m_busy) return;
    const QString localDir = localizeDir(dir);
    m_lastDownloadDir.clear();
    if (m_cancelPending) {
        mc_qt_download_set_cancel(false);
        m_cancelPending = false;
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_busy = true;
    m_progress = 0.0;
    m_currentTask.clear();
    m_subTask.clear();
    m_totalFiles = 0;
    m_completedFiles = 0;
    m_speedBytes = 0.0;
    emit busyChanged();
    emit progressChanged(0.0, QString());
    emit totalFilesChanged();
    emit completedFilesChanged();
    emit speedBytesChanged();

    QString javaDir = QDir(localDir).filePath(QStringLiteral(".runtime/java-%1").arg(majorVersion));

    auto *worker = new JavaDownloadWorker(majorVersion, javaDir);
    connect(worker, &JavaDownloadWorker::progressChanged, this, &DownloadManager::onWorkerProgress);
    connect(worker, &JavaDownloadWorker::subTaskChanged, this, &DownloadManager::onWorkerSubTask);
    connect(worker, &JavaDownloadWorker::totalFilesChanged, this, &DownloadManager::onWorkerTotalFiles);
    connect(worker, &JavaDownloadWorker::completedFilesChanged, this, &DownloadManager::onWorkerCompletedFiles);
    connect(worker, &JavaDownloadWorker::finished, this, [this](bool ok, const QString &javaPath, int majorVer) {
        m_busy = false;
        emit busyChanged();
        if (ok && !javaPath.isEmpty()) {
            emit javaDownloaded(majorVer, javaPath);
            emit allCompleted(true);
        } else {
            emit errorOccurred(QStringLiteral("Java %1 download failed").arg(majorVer));
            emit allCompleted(false);
        }
        if (m_cancelPending) {
            mc_qt_download_set_cancel(false);
            m_cancelPending = false;
        }
    });

    m_workerThread = new QThread(this);
    worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started, worker, &JavaDownloadWorker::run);
    connect(worker, &JavaDownloadWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, worker, &QObject::deleteLater);
    m_workerThread->start();
}

void DownloadManager::cancelAll()
{
    mc_qt_download_set_cancel(true);
    m_cancelPending = true;
    if (m_activeWorker) {
        m_activeWorker->cancel();
    }
}
