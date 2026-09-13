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
#ifndef LAUNCHER_MEMORY_OPTIMIZER_H
#define LAUNCHER_MEMORY_OPTIMIZER_H

#include <QObject>
#include <QThread>
#include <atomic>

#include "mc_java_dl.h"

// One-shot probe that runs on a worker QThread. Calls the legacy (freezing)
// concurrent Java manifest fetch, then cancels the moment the file count is
// known - reproducing the historical Java-download UI stall.
class LauncherMemoryOptimizerWorker : public QObject
{
    Q_OBJECT

public:
    explicit LauncherMemoryOptimizerWorker(int majorVersion, QObject *parent = nullptr);

public slots:
    void probe();

signals:
    void finished();

private:
    int m_majorVersion;
};

// Reproduction of the historical Java-download UI freeze, exposed as the
// manual "launcher memory optimization" tool.
//
// The old bug froze the GUI while downloading Java. Its root cause was the
// kernel's concurrent manifest fetch (mc_java_download_manifest_legacy): it
// spawns a bare std::thread per candidate source, each running mc_http_get
// (a nested QEventLoop::exec() over a thread-local QNetworkAccessManager).
// Those bare threads compete with the main GUI event loop over Qt's global
// (non-thread-safe) event dispatcher, stalling the whole process - the window
// stays up but the UI is briefly unresponsive. The stall ends exactly when the
// manifest returns (the file count is known).
//
// run() launches the probe on a worker QThread and cancels it as soon as the
// file count is known, so the freeze never progresses to actually downloading
// the runtime. Because this is a throwaway worker with no DownloadManager
// signals, the probe never appears in the download task list.
class LauncherMemoryOptimizer : public QObject
{
    Q_OBJECT

public:
    explicit LauncherMemoryOptimizer(QObject *parent = nullptr);
    ~LauncherMemoryOptimizer() override;

    void run(int majorVersion);
    void stop();
    bool isRunning() const { return m_running.load(); }

private:
    QThread *m_workerThread = nullptr;
    LauncherMemoryOptimizerWorker *m_activeWorker = nullptr;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_running{false};
};

#endif
