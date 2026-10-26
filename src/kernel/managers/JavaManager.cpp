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
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <QTimer>

namespace {

#ifdef Q_OS_WIN
constexpr const char *kJavaExeRel = "bin/java.exe";
constexpr const char *kJavacName = "javac.exe";
constexpr const char *kJlinkName = "jlink.exe";
#else
constexpr const char *kJavaExeRel = "bin/java";
constexpr const char *kJavacName = "javac";
constexpr const char *kJlinkName = "jlink";
#endif

// Shallow probe of one install root: "<root>/<name>/bin/java" plus one nested
// level ("<root>/<name>/<nested>/bin/java"), which covers layouts such as
// "jdk-17", "jdk-17.0.2+8", "21.0.3-tem" and "jre/...". Deliberately bounded:
// nothing here recurses.
void appendJavaFromRoot(QStringList *out, const QString &root)
{
    if (root.isEmpty())
        return;
    const QDir dir(root);
    if (!dir.exists())
        return;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QDir base(entry.absoluteFilePath());
        out->append(base.filePath(QString::fromLatin1(kJavaExeRel)));
        const QFileInfoList nested = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &n : nested)
            out->append(QDir(n.absoluteFilePath()).filePath(QString::fromLatin1(kJavaExeRel)));
    }
}

// Candidate java executables, cheapest first (see findInstalledJava()).
QStringList javaScanCandidates(const QString &runtimeDir)
{
    QStringList out;

    // 1. Runtimes we manage ourselves (downloaded/bundled).
    if (!runtimeDir.isEmpty()) {
        const QDir rt(runtimeDir);
        const QStringList subs = rt.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sub : subs)
            out << rt.filePath(sub + QLatin1Char('/') + QString::fromLatin1(kJavaExeRel));
    }

    // 2. JAVA_HOME, then the PATH directories (which already point at a bin/).
    const QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty())
        out << QDir(javaHome).filePath(QString::fromLatin1(kJavaExeRel));

    const QString pathEnv = qEnvironmentVariable("PATH");
    const QStringList pathDirs = pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts);
#ifdef Q_OS_WIN
    const QString pathExe = QStringLiteral("java.exe");
#else
    const QString pathExe = QStringLiteral("java");
#endif
    for (const QString &dir : pathDirs)
        out << QDir(dir).filePath(pathExe);

#ifdef Q_OS_WIN
    // 3. The usual vendor install roots.
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");

    // Guard against empty env vars: appending blindly would turn "" into
    // "/Java", which QDir resolves against the current drive root.
    const auto addRoot = [&out](const QString &base, const QString &name) {
        if (!base.isEmpty())
            appendJavaFromRoot(&out, base + QLatin1Char('/') + name);
    };
    addRoot(programFiles, QStringLiteral("Java"));
    addRoot(programFilesX86, QStringLiteral("Java"));
    addRoot(programFiles, QStringLiteral("Eclipse Adoptium"));
    addRoot(programFiles, QStringLiteral("Eclipse Foundation"));
    addRoot(programFiles, QStringLiteral("Microsoft"));
    addRoot(programFiles, QStringLiteral("Zulu"));
    addRoot(programFiles, QStringLiteral("BellSoft"));
    addRoot(localAppData, QStringLiteral("Programs/Eclipse Adoptium"));
#else
    appendJavaFromRoot(&out, QStringLiteral("/usr/lib/jvm"));
    appendJavaFromRoot(&out, QStringLiteral("/usr/lib64/jvm"));
    appendJavaFromRoot(&out, QStringLiteral("/Library/Java/JavaVirtualMachines"));
    appendJavaFromRoot(&out, QDir::homePath() + "/.sdkman/candidates/java");
    appendJavaFromRoot(&out, QStringLiteral("/opt"));
#endif

    out.removeDuplicates();
    return out;
}

// "17.0.2" → 17, "21-ea" → 21, "1.8.0_381" → 8, "1.6.0_45" → 6.
int majorFromVersionString(const QString &version)
{
    const QStringList parts = version.split(QRegularExpression(QStringLiteral("[^0-9]+")),
                                            Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return -1;
    bool ok = false;
    const int first = parts.first().toInt(&ok);
    if (!ok || first <= 0)
        return -1;
    if (first == 1 && parts.size() > 1) {
        const int legacy = parts.at(1).toInt(&ok);
        if (ok && legacy > 0)
            return legacy;
    }
    return first;
}

// Runs `<java> -version` and returns the major version, or -1 if the binary is
// missing or does not behave like a JVM.
int probeJavaMajor(const QString &javaExe)
{
    if (javaExe.isEmpty() || !QFileInfo(javaExe).isFile())
        return -1;

    QProcess proc;
    proc.setProgram(javaExe);
    proc.setArguments({QStringLiteral("-version")});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForStarted(3000))
        return -1;
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(1000);
        return -1;
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression quoted(QStringLiteral("version\\s+\"([^\"]+)\""));
    const QRegularExpressionMatch m = quoted.match(output);
    if (m.hasMatch()) {
        const int major = majorFromVersionString(m.captured(1));
        if (major > 0)
            return major;
    }
    // Some builds print something unexpected; fall back to the first number.
    static const QRegularExpression anyNum(QStringLiteral("\\b(\\d+)\\b"));
    const QRegularExpressionMatch n = anyNum.match(output);
    if (n.hasMatch()) {
        const int major = n.captured(1).toInt();
        if (major > 0)
            return major;
    }
    return -1;
}

} // namespace

QString JavaManager::findInstalledJava(const QString &runtimeDir, int exactMajor)
{
    const QStringList candidates = javaScanCandidates(runtimeDir);
    const QString runtimeRoot = runtimeDir.isEmpty() ? QString()
                                                     : QFileInfo(runtimeDir).absoluteFilePath();

    QString bestPath;
    int bestMajor = -1;
    bool bestIsJdk = false;

    for (const QString &candidate : candidates) {
        if (candidate.isEmpty())
            continue;

        const int major = probeJavaMajor(candidate);
        if (major <= 0)
            continue;

        const QString clean = QFileInfo(candidate).absoluteFilePath();
        const bool bundled = !runtimeRoot.isEmpty()
                             && clean.startsWith(runtimeRoot + QLatin1Char('/'),
                                                 Qt::CaseInsensitive);
        const QString binDir = QFileInfo(clean).absolutePath();
        const QString runtimeHome = QFileInfo(binDir).absolutePath();
        const bool isJdk =
            QFileInfo(binDir + QLatin1Char('/') + QString::fromLatin1(kJavacName)).isFile()
            || QFileInfo(binDir + QLatin1Char('/') + QString::fromLatin1(kJlinkName)).isFile();

        if (bundled && !isJdk
            && !QFileInfo(runtimeHome + QStringLiteral("/release")).isFile()) {
            // We only ever extract full downloads into the runtime directory.
            // A JDK has javac/jlink; a JRE-only bundle (Mojang's jre-legacy for
            // Java 8) ships a "release" marker instead. A runtime with neither
            // is a leftover from an interrupted download and must not be used.
            mc_info("[Java] Ignoring incomplete bundled runtime: %s",
                    clean.toUtf8().constData());
            continue;
        }

        if (exactMajor > 0) {
            if (major == exactMajor) {
                mc_info("[Java] Bounded scan matched Java %d%s: %s", major,
                        bundled ? " (bundled)" : "", clean.toUtf8().constData());
                return clean;
            }
            continue;
        }

        // "Best available" mode (used by the Forge installer, which needs any
        // usable JVM). Remember the best so far and stop early on a modern JDK.
        if (major < 8)
            continue;
        if (bestPath.isEmpty() || major > bestMajor
            || (major == bestMajor && isJdk && !bestIsJdk)) {
            bestPath = clean;
            bestMajor = major;
            bestIsJdk = isJdk;
        }
        if (bestMajor >= 17 && bestIsJdk)
            break;
    }

    if (!bestPath.isEmpty())
        mc_info("[Java] Bounded scan best Java %d: %s", bestMajor,
                bestPath.toUtf8().constData());
    return bestPath;
}

JavaManager::JavaManager(QObject *parent) : QObject(parent) {}
JavaManager::~JavaManager()
{
    if (m_findThread) {
        m_findThread->quit();
        if (!m_findThread->wait(3000))
            m_findThread->terminate();
        delete m_findThread;
        m_findThread = nullptr;
    }
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

    // Run mc_java_find_all on a dedicated worker thread so the GUI event loop
    // is not blocked by the directory scans + java -version subprocess probes
    // that this function performs (a full scan can take several seconds).
    //
    // Note: the scan gets its own thread member (m_findThread); the download
    // path owns m_workerThread, and sharing one pointer used to leave it
    // dangling (or tear down the wrong thread) once a scan finished.
    QThread *thread = new QThread(this);
    m_findThread = thread;
    QObject *worker = new QObject;
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_findThread == thread)
            m_findThread = nullptr;
    }, Qt::QueuedConnection);
    connect(thread, &QThread::started, worker, [this]() { doFindJavaOnThread(); });
    thread->start();
}

void JavaManager::doFindJavaOnThread()
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

    // Write back on the GUI thread so signals are emitted from the main thread
    // (as required by Qt's signal-slot rules).
    QMetaObject::invokeMethod(this, [this, list]() {
        m_runtimes = list;
        m_searching = false;
        emit runtimesChanged();
        emit searchingChanged();
    }, Qt::QueuedConnection);
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

    // Re-entrancy guard: the auto-download path reaches here from the main
    // thread (Qt::QueuedConnection) and can be re-entered while a previous
    // download is still finishing. A busy guard keeps the teardown below from
    // ever running on the main thread.
    if (m_searching) {
        mc_info("Java download already in progress, ignoring ver=%d", majorVersion);
        return;
    }

    // Clear any stale global cancel left by a previous cancelled download so
    // this fresh download does not immediately fail-fast on every piece.
    // (Mirrors DownloadManager which clears m_cancelPending at start.)
    if (mc_qt_download_cancel())
        mc_qt_download_set_cancel(false);

    // Stop any previous download worker to avoid multi-thread race.
    // If the previous download was cancelled, clean up its partial files.
    //
    // This whole function runs on the main thread in the auto-download path,
    // so a blocking wait() here is what froze the GUI. Tear the stale worker
    // down non-blockingly: cancel + quit + deleteLater, never wait(). The old
    // worker's in-flight pieces bail out via the global cancel flag and the
    // thread + worker are reclaimed by the event loop via deleteLater.
    if (m_workerThread) {
        bool prevCancelled = m_cancelled;
        QString prevDir = m_activeJavaWorker ? m_activeJavaWorker->targetDir() : QString();
        if (m_activeJavaWorker) m_activeJavaWorker->cancel();
        m_activeJavaWorker = nullptr;
        m_cancelled = false;
        // Ask the old worker to stop; it will quit its thread and self-delete.
        mc_qt_download_set_cancel(true);
        m_workerThread->quit();
        QThread *oldThread = m_workerThread;
        QObject::connect(oldThread, &QThread::finished, this,
                         [this, prevCancelled, prevDir]() {
                             finishStoppedWorker(prevCancelled, prevDir);
                         });
        oldThread->deleteLater();
        m_workerThread = nullptr;
    }

    // The teardown above trips the global cancel flag to stop the previous
    // worker; clear it again, otherwise every piece of the fresh download would
    // fail-fast (this is what made a launch after a cancelled download die).
    if (mc_qt_download_cancel())
        mc_qt_download_set_cancel(false);

    m_searching = true;
    emit searchingChanged();

    // Reset the progress state so the launch page never shows a percentage
    // left over from an earlier download while the new one is still fetching
    // its manifest.
    m_downloadProgress = 0.0;
    m_downloadTask.clear();
    m_totalFiles = 0;
    m_completedFiles = 0;
    m_speedBytes = 0.0;
    emit downloadProgressChanged(m_downloadProgress);
    emit downloadTaskChanged(m_downloadTask);
    emit totalFilesChanged();
    emit completedFilesChanged();
    emit speedBytesChanged();

    // Boost download-pool concurrency while Java is downloading so the
    // runtime bundle (typically 300+ files) doesn't share the pool with
    // background manifest/version-fetch work. Restore the user's setting
    // once the worker finishes.
    mc_qt_download_set_thread_limit(64);
    m_downloadBusy = true;
    emit downloadBusyChanged();

    QString targetDir = m_runtimeDir + QString("/java-%1").arg(majorVersion);

    m_workerThread = new QThread(this);
    QThread *downloadThread = m_workerThread;
    auto *worker = new JavaDownloadWorker(majorVersion, targetDir);
    m_activeJavaWorker = worker;
    m_cancelled = false;
    worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, worker, &JavaDownloadWorker::run);
    connect(worker, &JavaDownloadWorker::progressChanged, this, [this](qreal p, const QString &t) {
        m_downloadProgress = p;
        m_downloadTask = t;
        m_downloadBusy = true;
        emit downloadProgressChanged(p);
        emit downloadTaskChanged(t);
        emit downloadBusyChanged();
    });
    connect(worker, &JavaDownloadWorker::subTaskChanged, this, [this](const QString &t) {
        m_downloadTask = t;
        emit downloadTaskChanged(t);
    });
    connect(worker, &JavaDownloadWorker::totalFilesChanged, this, [this](int total) {
        m_totalFiles = total;
        emit totalFilesChanged();
    });
    connect(worker, &JavaDownloadWorker::completedFilesChanged, this, [this](int completed) {
        m_completedFiles = completed;
        emit completedFilesChanged();
    });
    connect(worker, &JavaDownloadWorker::finished, this, [this](bool ok, const QString &path, int ver) {
        m_searching = false;
        m_activeJavaWorker = nullptr;
        m_cancelled = false;
        mc_qt_download_set_cancel(false);
        mc_qt_download_set_thread_limit(16);
        m_downloadBusy = false;
        emit searchingChanged();
        emit downloadBusyChanged();

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
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    // Clear the member when *this* thread dies. The identity check keeps a
    // late "finished" from a stopped worker from wiping a newer download's
    // thread pointer (which used to leave a dangling pointer behind).
    connect(downloadThread, &QThread::finished, this, [this, downloadThread]() {
        if (m_workerThread == downloadThread)
            m_workerThread = nullptr;
    }, Qt::QueuedConnection);
    m_workerThread->start();
}

void JavaManager::cancelDownloadJava()
{
    if (!m_workerThread) return;
    mc_qt_download_set_cancel(true);
    mc_qt_download_set_thread_limit(16);
    m_cancelled = true;
    if (m_activeJavaWorker)
        m_activeJavaWorker->cancel();
    m_searching = false;
    m_downloadBusy = false;
    emit searchingChanged();
    emit downloadBusyChanged();
}

void JavaManager::finishStoppedWorker(bool prevCancelled, const QString &prevDir)
{
    // A newer download already owns the shared state (busy flag, cancel flag,
    // thread limit): touching it here would clobber the fresh download.
    if (m_workerThread)
        return;
    mc_qt_download_set_cancel(false);
    mc_qt_download_set_thread_limit(16);
    m_downloadBusy = false;
    emit downloadBusyChanged();
    if (prevCancelled && !prevDir.isEmpty())
        QDir(prevDir).removeRecursively();
}
