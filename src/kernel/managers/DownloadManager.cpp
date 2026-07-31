#include "DownloadManager.h"
#include "DownloadWorker.h"
#include <mc_log.h>
#include <mc_java_dl.h>
#include <mc_path.h>
#include <mc_download.h>
#include <mc_download_qt.h>

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <cstdlib>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {}
DownloadManager::~DownloadManager()
{
    if (m_workerThread) {
        if (m_activeWorker) m_activeWorker->cancel();
        m_workerThread->quit();
        if (!m_workerThread->wait(3000)) {
            m_workerThread->terminate();
            m_workerThread->wait(3000);
        }
    }
}

void DownloadManager::startDownload(const QString &versionId, const QString &dir)
{
    if (m_busy) return;
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

    auto *worker = new DownloadWorker(versionId, dir);
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

    QTimer::singleShot(0, this, [this, majorVersion, dir]() {
        McJavaFileList list;
        memset(&list, 0, sizeof(list));

        if (!mc_java_download_manifest(majorVersion, "bmclapi", &list)) {
            m_busy = false;
            emit busyChanged();
            emit errorOccurred(QStringLiteral("Failed to fetch Java %1 manifest").arg(majorVersion));
            emit allCompleted(false);
            return;
        }

        QString javaDir = QDir(dir).filePath(QStringLiteral(".runtime/java-%1").arg(majorVersion));
        QDir().mkpath(javaDir);

        static const QSet<QByteArray> skipHashes = {
            QByteArray::fromHex("12976a6c2b227cbac58969c1455444596c894656"),
            QByteArray::fromHex("c80e4bab46e34d02826eab226a4441d0970f2aba"),
            QByteArray::fromHex("84d2102ad171863db04e7ee22a259d1f6c5de4a5")
        };

        // Store with both mirror (BMCLAPI) and original (Mojang) URLs
        struct FileEntry {
            QByteArray mirrorUrl;
            QByteArray originalUrl;
            QByteArray path;
            QByteArray sha1;
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
            if (mc_download_translate_mojang_url(f.url, mirrorBuf, sizeof(mirrorBuf), "bmclapi")) {
                e.mirrorUrl = QByteArray(mirrorBuf);
                if (i < 3)
                    mc_info("[DL] Java URL #%d: original=%.120s mirror=%.120s", i, f.url, mirrorBuf);
            } else {
                e.mirrorUrl = e.originalUrl;
                mc_info("[DL] Java URL #%d: mirror translation FAILED, using original: %.120s", i, f.url);
            }
            e.path = fullPath.toUtf8();
            e.sha1 = QByteArray(f.sha1);
            pending.push_back(std::move(e));
        }

        m_totalFiles = static_cast<int>(pending.size()) + completed;
        m_completedFiles = completed;
        emit totalFilesChanged();
        emit completedFilesChanged();

        if (!pending.empty()) {
            for (const auto &e : pending) {
                char d[1024];
                mc_path_dirname(e.path.constData(), d, sizeof(d));
                mc_path_mkdir_p(d);
            }
        }

        // PCLCE pattern: batch in chunks for real-time progress, then individual retry
        if (!pending.empty()) {
            const int chunkSize = 16;
            int total = static_cast<int>(pending.size());
            std::vector<int> results(total, 0);

            // Phase 1: batch download in chunks
            m_currentTask = QStringLiteral("Downloading Java %1...").arg(majorVersion);
            emit progressChanged(m_progress, m_currentTask);

            for (int chunkStart = 0; chunkStart < total; chunkStart += chunkSize) {
                int chunkEnd = std::min(chunkStart + chunkSize, total);
                int chunkCount = chunkEnd - chunkStart;

                m_subTask = QStringLiteral("%1-%2 / %3")
                    .arg(chunkStart + 1).arg(chunkEnd).arg(total);
                emit subTaskChanged();

                std::vector<const char *> cUrls, cPaths, cSha1s;
                std::vector<long> sizes(chunkCount, 0);
                cUrls.reserve(chunkCount); cPaths.reserve(chunkCount); cSha1s.reserve(chunkCount);
                for (int i = chunkStart; i < chunkEnd; ++i) {
                    cUrls.push_back(pending[i].mirrorUrl.constData());
                    cPaths.push_back(pending[i].path.constData());
                    cSha1s.push_back(pending[i].sha1.isEmpty() ? nullptr : pending[i].sha1.constData());
                }

                mc_qt_download_batch(cUrls.data(), cPaths.data(), cSha1s.data(),
                                     sizes.data(), chunkCount, 20000, &results[chunkStart]);

                int chunkOk = 0;
                for (int i = chunkStart; i < chunkEnd; ++i) {
                    if (results[i]) {
                        m_completedFiles++;
                        chunkOk++;
                    }
                }
                if (chunkOk < chunkCount) {
                    int failedIdx = chunkStart;
                    for (int i = chunkStart; i < chunkEnd; ++i) {
                        if (!results[i]) { failedIdx = i; break; }
                    }
                    mc_info("[DL] Chunk %d-%d: %d/%d ok, first failed path=%.80s",
                            chunkStart + 1, chunkEnd, chunkOk, chunkCount,
                            pending[failedIdx].path.constData());
                }
                m_progress = static_cast<qreal>(m_completedFiles) / m_totalFiles;
                emit progressChanged(m_progress, m_currentTask);
                emit completedFilesChanged();
            }

            // Phase 2: individual retry for failed files
            std::vector<FileEntry> failures;
            for (int i = 0; i < total; ++i) {
                if (!results[i])
                    failures.push_back(pending[i]);
            }

            if (!failures.empty()) {
                m_subTask = QStringLiteral("Retrying %1 failed files...").arg(failures.size());
                emit subTaskChanged();
                mc_info("[DL] Java %d: retrying %d files with mirror→original fallback...", majorVersion, (int)failures.size());
                for (auto &fe : failures) {
                    const char *rUrls[2] = {fe.mirrorUrl.constData(), fe.originalUrl.constData()};
                    bool rOk = false;
                    for (int r = 0; r < 3 && !rOk; ++r) {
                        QThread::msleep(200 + (std::rand() % (100 + r * 200)));
                        rOk = mc_qt_download_file_multi(rUrls, 2, fe.path.constData(),
                                                         fe.sha1.isEmpty() ? nullptr : fe.sha1.constData(), 0, 15000);
                        if (!rOk && r < 2)
                            mc_info("[DL]   attempt %d failed, next...", r + 1);
                    }
                    int plen = (int)strlen(fe.path.constData());
                    const char *pshort = plen > 40 ? fe.path.constData() + plen - 40 : fe.path.constData();
                    mc_info("[DL] Retry ...%s: %s",
                            pshort, rOk ? "OK" : "FAIL");
                    if (rOk) m_completedFiles++;
                    m_progress = static_cast<qreal>(m_completedFiles) / m_totalFiles;
                    emit progressChanged(m_progress, m_currentTask);
                    emit completedFilesChanged();
                }
                emit subTaskChanged();
            }
        }

        mc_java_file_list_free(&list);

        mc_info("[DL] Java %d done: %d/%d files ok, %d skipped already present",
                majorVersion, m_completedFiles, m_totalFiles, completed);

        m_busy = false;
        emit busyChanged();

        int totalPending = static_cast<int>(pending.size());
        bool allOk = (totalPending == 0) || (m_completedFiles >= m_totalFiles);
        if (!allOk) {
            QDir(javaDir).removeRecursively();
            mc_info("[DL] Java %d download failed, cleaned up %s", majorVersion, javaDir.toUtf8().constData());
            emit errorOccurred(QStringLiteral("Java %1 download failed").arg(majorVersion));
            emit allCompleted(false);
        } else {
            QString javaPath = QDir(javaDir).filePath("bin/java.exe");
            emit javaDownloaded(majorVersion, javaPath);
            emit allCompleted(true);
        }
    });
}

void DownloadManager::cancelAll()
{
    if (m_activeWorker) {
        m_activeWorker->cancel();
    }
}
