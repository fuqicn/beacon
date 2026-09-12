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
#include "JavaManager.h"
#include "JavaDownloadWorker.h"
#include <mc_log.h>
#include <mc_java.h>
#include <mc_download_qt.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

JavaManager::JavaManager(QObject *parent) : QObject(parent) {}
JavaManager::~JavaManager()
{
    if (m_workerThread) {
        if (m_activeJavaWorker) m_activeJavaWorker->cancel();
        m_activeJavaWorker = nullptr;
        m_workerThread->quit();
        if (!m_workerThread->wait(5000))
            m_workerThread->wait(5000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

void JavaManager::setRuntimeDir(const QString &dir)
{
    if (m_runtimeDir != dir) {
        m_runtimeDir = dir;
        QDir().mkpath(dir);
        emit runtimeDirChanged();
    }
}

void JavaManager::findJavaAsync()
{
    if (m_searching) return;
    m_searching = true;
    emit searchingChanged();

    QTimer::singleShot(0, this, [this]() { doFindJava(); });
}

void JavaManager::doFindJava()
{
    McJavaRuntime runtimes[128];
    int count = mc_java_find_all(runtimes, 128);
    mc_info("Found %d Java runtimes", count);

    QVariantList list;
    for (int i = 0; i < count; ++i) {
        QVariantMap rt;
        rt["path"] = QString::fromUtf8(runtimes[i].path);
        rt["majorVersion"] = runtimes[i].major_version;
        rt["version"] = QString::fromUtf8(runtimes[i].version);
        rt["vendor"] = QString::fromUtf8(runtimes[i].vendor);
        rt["is64bit"] = (bool)runtimes[i].is_64bit;
        rt["isJdk"] = (bool)runtimes[i].is_jdk;
        list.append(rt);
    }

    m_runtimes = list;
    m_searching = false;
    emit runtimesChanged();
    emit searchingChanged();
}

void JavaManager::findJava()
{
    findJavaAsync();
}

void JavaManager::addBundledJava()
{
    if (m_runtimeDir.isEmpty()) return;

    // Filter duplicates by path before appending
    QStringList existingPaths;
    for (const auto &e : m_runtimes)
        existingPaths << e.toMap().value("path").toString();

    bool changed = false;
    QDir rtDir(m_runtimeDir);
    QStringList subdirs = rtDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &sub : subdirs) {
        QString javaPath;
#ifdef Q_OS_WIN
        javaPath = rtDir.filePath(sub + "/bin/java.exe");
#else
        javaPath = rtDir.filePath(sub + "/bin/java");
#endif
        if (QFile::exists(javaPath) && !existingPaths.contains(javaPath)) {
            QVariantMap rt;
            rt["path"] = javaPath;
            rt["majorVersion"] = 0;
            rt["version"] = sub;
            rt["vendor"] = "Bundled";
            rt["is64bit"] = true;
            rt["isJdk"] = false;
            m_runtimes.append(rt);
            changed = true;
        }
    }

    if (changed)
        emit runtimesChanged();
}

QVariantMap JavaManager::findBest() const
{
    McJavaRuntime best;
    if (mc_java_find_best(&best)) {
        QVariantMap rt;
        rt["path"] = QString::fromUtf8(best.path);
        rt["majorVersion"] = best.major_version;
        rt["version"] = QString::fromUtf8(best.version);
        rt["vendor"] = QString::fromUtf8(best.vendor);
        return rt;
    }
    return QVariantMap();
}

void JavaManager::downloadJava(int majorVersion)
{
    if (m_runtimeDir.isEmpty()) {
        emit errorOccurred("Runtime directory not set");
        emit javaDownloadFailed(majorVersion);
        return;
    }

    // Clear any stale global cancel left by a previous cancelled download so
    // this fresh download does not immediately fail-fast on every piece.
    // (Mirrors DownloadManager which clears m_cancelPending at start.)
    mc_qt_download_set_cancel(false);

    // Stop any previous download worker to avoid multi-thread race and UI freeze.
    // If the previous download was cancelled, clean up its partial files.
    // Never terminate: the kernel manifest fetch spawns std::threads that would
    // crash if the QThread were force-killed mid-join.
    //
    // The global cancel flag is shared across every download module, so a stale
    // "cancelled" state left by a previous (or concurrently cancelled) task
    // makes every in-flight piece fail-fast and the download stalls at 0%.
    // Pair a cancel here so this fresh download starts clean, mirroring
    // DownloadManager which clears m_cancelPending at start.
    if (mc_qt_download_cancel())
        mc_qt_download_set_cancel(false);

    if (m_workerThread) {
        bool prevCancelled = m_cancelled;
        QString prevDir = m_activeJavaWorker ? m_activeJavaWorker->targetDir() : QString();
        if (m_activeJavaWorker) m_activeJavaWorker->cancel();
        m_activeJavaWorker = nullptr;
        // Mark a pending release of the global flag; the finishing worker will
        // clear it once all its in-flight pieces have bailed out.
        mc_qt_download_set_cancel(true);
        m_workerThread->quit();
        if (!m_workerThread->wait(5000))
            m_workerThread->wait(5000);
        delete m_workerThread;
        m_workerThread = nullptr;
        mc_qt_download_set_cancel(false);
        if (prevCancelled && !prevDir.isEmpty())
            QDir(prevDir).removeRecursively();
    }

    m_searching = true;
    emit searchingChanged();

    QString targetDir = m_runtimeDir + QString("/java-%1").arg(majorVersion);

    m_workerThread = new QThread(this);
    auto *worker = new JavaDownloadWorker(majorVersion, targetDir);
    m_activeJavaWorker = worker;
    m_cancelled = false;
    worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, worker, &JavaDownloadWorker::run);
    connect(worker, &JavaDownloadWorker::finished, this, [this](bool ok, const QString &path, int ver) {
        m_searching = false;
        m_activeJavaWorker = nullptr;
        m_cancelled = false;
        mc_qt_download_set_cancel(false);
        emit searchingChanged();

        if (ok && QFile::exists(path)) {
            QVariantMap rt;
            rt["path"] = path;
            rt["majorVersion"] = ver;
            rt["version"] = QString("Java %1 (Bundled)").arg(ver);
            rt["vendor"] = "Bundled";
            rt["is64bit"] = true;
            rt["isJdk"] = false;
            m_runtimes.append(rt);
            emit runtimesChanged();
            emit javaDownloaded(path, ver);
        } else {
            emit javaDownloadFailed(ver);
        }
    });
    connect(worker, &JavaDownloadWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, worker, &QObject::deleteLater);

    m_workerThread->start();
}

void JavaManager::cancelDownloadJava()
{
    if (!m_workerThread) return;
    mc_qt_download_set_cancel(true);
    m_cancelled = true;
    if (m_activeJavaWorker)
        m_activeJavaWorker->cancel();
    m_searching = false;
    emit searchingChanged();
}
