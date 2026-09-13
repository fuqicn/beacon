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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "LauncherMemoryOptimizer.h"

#include <mc_log.h>
#include <mc_download.h>

#include <QThread>
#include <cstring>

LauncherMemoryOptimizer::LauncherMemoryOptimizer(QObject *parent)
    : QObject(parent)
{
}

LauncherMemoryOptimizer::~LauncherMemoryOptimizer()
{
    stop();
    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(5000))
            m_workerThread->terminate();
    }
}

void LauncherMemoryOptimizer::stop()
{
    m_cancelled = true;
}

void LauncherMemoryOptimizer::run(int majorVersion)
{
    if (m_running.load())
        return;
    m_cancelled = false;
    m_running = true;

    // Same worker-thread pattern as JavaManager::downloadJava: move the worker
    // onto a fresh QThread and fire probe() there. probe() is a one-shot task
    // that quits the thread when done; the thread and worker are reclaimed via
    // finished->deleteLater.
    m_workerThread = new QThread(this);
    m_activeWorker = new LauncherMemoryOptimizerWorker(majorVersion);

    QObject::connect(m_workerThread, &QThread::started,
                     m_activeWorker, &LauncherMemoryOptimizerWorker::probe);
    QObject::connect(m_activeWorker, &LauncherMemoryOptimizerWorker::finished,
                     m_workerThread, &QThread::quit);
    QObject::connect(m_workerThread, &QThread::finished,
                     m_activeWorker, &QObject::deleteLater);
    QObject::connect(m_workerThread, &QThread::finished,
                     m_workerThread, &QObject::deleteLater);
    QObject::connect(m_workerThread, &QThread::finished, this, [this]() {
        m_running = false;
        m_activeWorker = nullptr;
        if (m_onFinished)
            m_onFinished();
    });
    m_workerThread->start();
}

LauncherMemoryOptimizerWorker::LauncherMemoryOptimizerWorker(int majorVersion, QObject *parent)
    : QObject(parent), m_majorVersion(majorVersion)
{
}

void LauncherMemoryOptimizerWorker::probe()
{
    // Reproduce the historical Java-download freeze: run the legacy concurrent
    // manifest fetch on this worker thread. It spawns bare std::threads that
    // each embed a QEventLoop over a thread-local QNetworkAccessManager, which
    // stall the main GUI event dispatcher - the window stays up but the UI is
    // briefly unresponsive.
    //
    // The moment the manifest returns we know the file count, which is exactly
    // when the old freeze ended. So we stop right here and never download the
    // actual runtime files. Because this is a throwaway worker thread with no
    // DownloadManager signals, the probe never appears in the download list.
    mc_info("[MemOpt] legacy manifest probe start (ver=%d)", m_majorVersion);
    McJavaFileList list;
    memset(&list, 0, sizeof(list));
    bool ok = mc_java_download_manifest_legacy(m_majorVersion,
                                               mc_download_effective_mirror(),
                                               &list);
    const int probeCount = list.count;
    if (list.files)
        mc_java_file_list_free(&list);
    mc_info("[MemOpt] legacy manifest probe done: ok=%d files=%d (cancelled, no download)",
            ok ? 1 : 0, probeCount);

    emit finished();
}
