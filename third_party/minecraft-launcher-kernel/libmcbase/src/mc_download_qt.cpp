/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_download_qt.h"
#include "mc_hash.h"
#include "mc_path.h"
#include "mc_str.h"
#include "mc_log.h"
#include <QtCore/QCoreApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QHttp1Configuration>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QThread>
#include <QtCore/QElapsedTimer>
#include <QtNetwork/QHostInfo>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>

// ---------------------------------------------------------------------------
// Global persistent download pool.
//
// Every public download API dispatches its work to a fixed set of long-lived
// worker threads (g_thread_limit of them, created once at init / first use).
// Each pool thread owns a private QNetworkAccessManager pool and drives its own
// event loops, so callers never spin nested event loops themselves and QNAMs
// are never torn down while QNetworkReply objects are still in flight.
//
// This is the structural fix for two crash classes seen in the field:
//   1. Stack overflow (0xc00000fd) from deep nested per-caller event loops.
//   2. Access violation in Qt6Core from batch_ex worker threads deleting
//      their QNAM pools (mc_qt_download_cleanup) with live replies.
// ---------------------------------------------------------------------------

// Non-trivial thread_local objects get a __cxa_thread_atexit destructor via
// __tls_init; MinGW emutls frees the TLS block before running that dtor,
// causing a double-destroy at thread exit. Keep only a trivial pointer in
// thread_local storage and own the object on the heap (intentionally leaked,
// same lifetime as the QNAMs inside the pool).
static thread_local std::vector<QNetworkAccessManager*> *t_nam_pool = nullptr;
static thread_local size_t t_nam_sel{0};

static void ensure_pool_nams() {
    if (t_nam_pool) return;
    t_nam_pool = new std::vector<QNetworkAccessManager*>();
    int n = 4;
    t_nam_pool->reserve((size_t)n);
    for (int i = 0; i < n; i++)
        t_nam_pool->push_back(new QNetworkAccessManager());
}

static QNetworkAccessManager *pick_nam(void) {
    ensure_pool_nams();
    return (*t_nam_pool)[t_nam_sel++ % t_nam_pool->size()];
}

// Cooperative cancel flag: when set, request loops poll it and bail out
// promptly, letting worker threads finish cleanly instead of being terminated.
// Reference-counted so independent modules (download manager, modpack install,
// mod install) can cancel without clearing each other's in-flight cancellation.
static std::atomic<bool> g_cancel{false};
static std::atomic<int> g_cancel_refs{0};

void mc_qt_download_set_cancel(int v) {
    if (v) {
        if (g_cancel_refs.fetch_add(1) == 0)
            g_cancel.store(true);
    } else {
        int old = g_cancel_refs.fetch_sub(1);
        if (old <= 0) {
            g_cancel_refs.store(0);      // defensive: never go negative
        } else if (old == 1) {
            g_cancel.store(false);       // last holder released -> resume
        }
    }
}

int mc_qt_download_cancel(void) {
    return g_cancel ? 1 : 0;
}

static int g_thread_limit = 8;
static int g_max_pieces = 4;

void mc_qt_download_set_thread_limit(int n) {
    if (n < 1) n = 1;
    if (n > 64) n = 64;
    g_thread_limit = n;
}

int mc_qt_download_thread_limit(void) { return g_thread_limit; }

void mc_qt_download_set_max_pieces(int n) {
    if (n < 1) n = 1;
    if (n > 64) n = 64;
    g_max_pieces = n;
}

int mc_qt_download_max_pieces(void) { return g_max_pieces; }

// ---- Pool machinery ----

struct PoolJob {
    std::function<int()> run;
    std::shared_ptr<std::promise<int>> promise = std::make_shared<std::promise<int>>();
};

static std::atomic<bool> g_pool_stop{false};
static std::mutex g_pool_mtx;
static std::condition_variable g_pool_cv;
static std::deque<std::shared_ptr<PoolJob>> g_pool_queue;
static std::vector<std::thread> g_pool_threads;
static bool g_pool_started = false;

static void pool_shutdown();

static void pool_worker() {
    ensure_pool_nams();
    for (;;) {
        std::shared_ptr<PoolJob> job;
        {
            std::unique_lock<std::mutex> lk(g_pool_mtx);
            g_pool_cv.wait(lk, [] { return g_pool_stop || !g_pool_queue.empty(); });
            if (g_pool_stop && g_pool_queue.empty()) break;
            job = std::move(g_pool_queue.front());
            g_pool_queue.pop_front();
        }
        int r = 0;
        try { r = job->run(); } catch (...) { r = 0; }
        try { job->promise->set_value(r); } catch (...) {}
    }
}

static void pool_start_locked() {
    if (g_pool_started) return;
    g_pool_started = true;
    if (g_thread_limit < 1) g_thread_limit = 8;
    g_pool_threads.reserve((size_t)g_thread_limit);
    for (int i = 0; i < g_thread_limit; i++)
        g_pool_threads.emplace_back(pool_worker);
    static std::once_flag atexit_once;
    std::call_once(atexit_once, [] { std::atexit(pool_shutdown); });
}

static void pool_shutdown() {
    std::vector<std::thread> doomed;
    {
        std::lock_guard<std::mutex> lk(g_pool_mtx);
        if (!g_pool_started) return;
        g_pool_stop = true;
        g_cancel = true;
        doomed.swap(g_pool_threads);
    }
    g_pool_cv.notify_all();
    // Do NOT join the workers here. After pool_worker() returns, Qt network /
    // TLS thread-local teardown can keep a worker thread alive for tens of
    // seconds, and joining blocks the process shutdown path (the window is
    // already closed and the user expects an immediate exit). Cancel was set
    // above, so in-flight jobs abort within ~1s and workers break out of the
    // loop on their own; detach and let the OS reap them when main() returns.
    for (auto &t : doomed)
        if (t.joinable()) t.detach();
    std::lock_guard<std::mutex> lk(g_pool_mtx);
    g_pool_started = false;
    g_pool_stop = false;
}

static std::future<int> pool_submit(std::function<int()> fn) {
    auto job = std::make_shared<PoolJob>();
    job->run = std::move(fn);
    auto fut = job->promise->get_future();
    {
        std::lock_guard<std::mutex> lk(g_pool_mtx);
        pool_start_locked();
        g_pool_queue.push_back(std::move(job));
    }
    g_pool_cv.notify_one();
    return fut;
}

// Thread-local "no event pump" flag. Plain std::thread workers (e.g. the
// modpack pack-file thread) must NOT call QCoreApplication::processEvents:
// Qt's event processing is not safe to run concurrently from two threads and
// crashes inside Qt6Core. Those threads wait purely on the pool future while
// the pool's own threads (which own their event loops) do the download work.
static thread_local bool t_no_pump = false;

void mc_qt_download_thread_no_pump(int on) {
    t_no_pump = on != 0;
}

static void pump_events() {
    if (t_no_pump) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return;
    }
    if (QCoreApplication::instance())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    else
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

static void pool_wait(std::future<int> &fut) {
    while (fut.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
        pump_events();
}

// Progress samples produced by pool threads are drained on the calling thread
// (which blocks until the job completes), preserving the documented "called
// from the calling thread" contract.
struct ProgressRelay {
    std::mutex mtx;
    bool done = false;
    int result = 0;
    std::vector<std::pair<long long, long long>> samples;

    void report(long long received, long long total) {
        std::lock_guard<std::mutex> lk(mtx);
        samples.emplace_back(received, total);
    }
    void finish(int res) {
        std::lock_guard<std::mutex> lk(mtx);
        result = res;
        done = true;
    }
};

static int relay_wait(ProgressRelay &relay, McQtDownloadProgressFn fn, void *user) {
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(relay.mtx);
            for (auto &s : relay.samples) {
                if (fn) fn("", s.first, s.second, user);
            }
            relay.samples.clear();
            if (relay.done) return relay.result;
        }
        pump_events();
    }
}

// ---- Init / cleanup ----

void mc_qt_download_init(void) {
    std::lock_guard<std::mutex> lk(g_pool_mtx);
    pool_start_locked();
}

void mc_qt_download_cleanup(void) {
    pool_shutdown();
}

void mc_qt_dns_prefetch(void) {
    QThread *t = QThread::create([]() {
        const char *hosts[] = {
            "piston-meta.mojang.com","piston-data.mojang.com","launcher.mojang.com",
            "launchermeta.mojang.com","resources.download.minecraft.net","libraries.minecraft.net",
            "maven.fabricmc.net","maven.minecraftforge.net","maven.neoforged.net",
            "bmclapi2.bangbang93.com","api.modrinth.com",NULL
        };
        for (int i = 0; hosts[i]; i++) QHostInfo::fromName(QString::fromUtf8(hosts[i]));
    });
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

// ---- Temp file helpers ----

static void clean_temp(const char *path) {
    for (int i = 0; i < 200; i++) {
        char p[2048]; snprintf(p,sizeof(p),"%s.chunk.%d",path,i);
        QFile::remove(QString::fromUtf8(p));
    }
    char t[2048]; snprintf(t,sizeof(t),"%s%s",path,".PCLDownloading");
    QFile::remove(QString::fromUtf8(t));
}

static int merge_file(const char *out, long long expect_size, const char *expect_sha1) {
    int n = 0;
    for (int i = 0; i < 200; i++) {
        char p[2048]; snprintf(p,sizeof(p),"%s.chunk.%d",out,i);
        if (mc_path_exists(p)) n++; else break;
    }
    if (n == 0) return 0;

    char tmp[2048]; snprintf(tmp,sizeof(tmp),"%s%s",out,".PCLDownloading");
    FILE *fo = fopen(tmp,"wb");
    if (!fo) { clean_temp(out); return 0; }
    long long written = 0;
    for (int i = 0; i < n; i++) {
        char cp[2048]; snprintf(cp,sizeof(cp),"%s.chunk.%d",out,i);
        FILE *fi = fopen(cp,"rb");
        if (!fi) { fclose(fo); clean_temp(out); return 0; }
        fseek(fi,0,SEEK_END); long long sz = ftell(fi); fseek(fi,0,SEEK_SET);
        char buf[65536];
        while (sz > 0) {
            long long rd = sz > (long long)sizeof(buf) ? (long long)sizeof(buf) : sz;
            size_t nr = fread(buf,1,(size_t)rd,fi);
            if (nr == 0) break;
            fwrite(buf,1,nr,fo); written += (long long)nr; sz -= (long long)nr;
        }
        fclose(fi); QFile::remove(QString::fromUtf8(cp));
    }
    fclose(fo);

    if (expect_size > 0 && written != expect_size) {
        mc_info("[DL-M] merge SIZE MISMATCH %s: written=%lld expect=%lld", out, written, expect_size);
        QFile::remove(QString::fromUtf8(tmp)); clean_temp(out); return 0;
    }
    if (expect_sha1 && *expect_sha1) {
        char sha[64];
        if (mc_hash_file_sha1(tmp,sha,sizeof(sha)) && mc_stricmp(sha,expect_sha1) != 0) {
            mc_info("[DL-M] merge SHA1 MISMATCH %s", out);
            QFile::remove(QString::fromUtf8(tmp)); clean_temp(out); return 0;
        }
    }
    if (QFile::rename(QString::fromUtf8(tmp),QString::fromUtf8(out))) return 1;
    FILE *sf = fopen(tmp,"rb"), *df = fopen(out,"wb");
    if (sf && df) {
        char buf[65536]; size_t nr;
        while ((nr = fread(buf,1,sizeof(buf),sf)) > 0) fwrite(buf,1,nr,df);
        fclose(sf); fclose(df); QFile::remove(QString::fromUtf8(tmp)); return 1;
    }
    mc_info("[DL-M] merge rename FAILED %s", out);
    clean_temp(out); return 0;
}

static void setup_req(QNetworkRequest &req, long timeout_ms) {
    req.setTransferTimeout((int)timeout_ms);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "*/*");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QHttp1Configuration h1;
    h1.setNumberOfConnectionsPerHost(64);
    req.setHttp1Configuration(h1);
}

// BMCLAPI asks clients to throttle requests; PCL sleeps ~100ms between starts
// across two starter threads, so the effective rate is ~20/s.
static std::mutex g_throttle_mtx;
static std::chrono::steady_clock::time_point g_throttle_last;
static void bmclapi_throttle(const char *url) {
    if (!url || !strstr(url, "bmclapi")) return;
    std::lock_guard<std::mutex> lk(g_throttle_mtx);
    auto now = std::chrono::steady_clock::now();
    auto gap = now - g_throttle_last;
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(gap).count();
    if (ms < 50) QThread::msleep((unsigned long)(50 - ms));
    g_throttle_last = std::chrono::steady_clock::now();
}

struct DlStream {
    FILE *fp{nullptr};
    long long written{0};
};

// GET one byte range (or the whole file when off < 0) on a pool thread,
// streaming to sink_path. Expects exactly 'len' bytes when len > 0.
// Returns 0 on success. When allow_slow_abort is set and the source stalls
// below the throughput floor, the request is aborted so the caller can try
// the next source instead of waiting forever on a throttled one.
// 'pr' (nullable) collects progress samples for the calling thread.
static int download_piece(const char *url, const char *sink_path,
                          long off, long len, long timeout_ms,
                          bool allow_slow_abort, ProgressRelay *pr) {
    const long long SLOW_GRACE_MS = 8000;
    const long long STALL_ABORT_MS = 30000;
    char dir[1024];
    mc_path_dirname(sink_path, dir, sizeof(dir));
    mc_path_mkdir_p(dir);

    FILE *fp = fopen(sink_path, "wb");
    if (!fp) return 1;

    auto stream = std::make_shared<DlStream>();
    stream->fp = fp;
    auto ok_flag = std::make_shared<bool>(false);
    auto timed_out = std::make_shared<bool>(false);

    bmclapi_throttle(url);

    QUrl qurl(QString::fromUtf8(url));
    QNetworkRequest req(qurl);
    setup_req(req, timeout_ms);
    if (off >= 0 && len > 0)
        req.setRawHeader("Range", QString("bytes=%1-%2").arg(off).arg(off + len - 1).toUtf8());

    QNetworkReply *reply = pick_nam()->get(req);
    QEventLoop loop;
    QTimer timer;
    QMetaObject::Connection tc;
    if (timeout_ms > 0) {
        timer.setSingleShot(true);
        tc = QObject::connect(&timer, &QTimer::timeout, [&loop, timed_out]() {
            *timed_out = true;
            loop.quit();
        });
        timer.start((int)timeout_ms);
    }

    auto t_start = std::chrono::steady_clock::now();
    auto slow_abort = std::make_shared<bool>(false);
    auto last_activity = std::make_shared<std::chrono::steady_clock::time_point>(t_start);

    QMetaObject::Connection rc = QObject::connect(reply, &QNetworkReply::readyRead, [reply, stream, len, &timer, timeout_ms, last_activity, pr]() {
        QByteArray data = reply->readAll();
        if (stream->fp) {
            stream->written += (long long)fwrite(data.constData(), 1, (size_t)data.size(), stream->fp);
        }
        if (pr) pr->report(stream->written, len > 0 ? len : -1);
        *last_activity = std::chrono::steady_clock::now();
        if (timeout_ms > 0) timer.start((int)timeout_ms);
    });
    QMetaObject::Connection fc = QObject::connect(reply, &QNetworkReply::finished, [reply, stream, &loop, ok_flag, timed_out, len]() {
        if (!*timed_out && reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (!data.isEmpty() && stream->fp) {
                stream->written += (long long)fwrite(data.constData(), 1, (size_t)data.size(), stream->fp);
            }
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200 || status == 206) {
                if (len <= 0 || stream->written == len)
                    *ok_flag = true;
            }
        }
        reply->deleteLater();
        loop.quit();
    });

    QTimer slow_timer;
    QMetaObject::Connection swc;
    {
        slow_timer.setInterval(1000);
        swc = QObject::connect(&slow_timer, &QTimer::timeout,
                               [&loop, reply, stream, timed_out, ok_flag, slow_abort,
                                last_activity, t_start, allow_slow_abort]() {
            if (*timed_out || *ok_flag || !allow_slow_abort) return;
            auto now = std::chrono::steady_clock::now();
            long long idle_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_activity).count();
            long long tot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_start).count();
            // Abort only when the source makes NO progress at all for a sustained
            // window (PCL BlockTimeout behavior). Slow-but-flowing sources are
            // allowed to run as long as bytes keep arriving.
            if (tot_ms > SLOW_GRACE_MS && idle_ms >= STALL_ABORT_MS) {
                *slow_abort = true;
                loop.quit();
            }
        });
        slow_timer.start();
    }

    QTimer cancel_timer;
    QMetaObject::Connection cc;
    {
        cancel_timer.setInterval(100);
        cc = QObject::connect(&cancel_timer, &QTimer::timeout, [&loop, timed_out]() {
            if (g_cancel) {
                *timed_out = true;
                loop.quit();
            }
        });
        cancel_timer.start();
    }

    loop.exec();
    timer.stop();
    {
        slow_timer.stop();
        QObject::disconnect(swc);
        cancel_timer.stop();
        QObject::disconnect(cc);
    }

    // Detach before returning so late signals cannot touch the dead loop.
    QObject::disconnect(rc);
    QObject::disconnect(fc);
    if (timeout_ms > 0) QObject::disconnect(tc);

    fclose(stream->fp); stream->fp = nullptr;

    if (*slow_abort)
        mc_info("[DL-Q] slow source aborted: %s", sink_path);
    if (!*ok_flag || *timed_out || *slow_abort) {
        // The reply is still in flight when we bailed out via a timer; abort
        // it here (outside the timer callback) and schedule cleanup.
        if (*timed_out || *slow_abort) {
            reply->abort();
            reply->deleteLater();
        }
        QFile::remove(QString::fromUtf8(sink_path));
        return 1;
    }
    return 0;
}

// Download a whole file's byte ranges in parallel inside a pool thread:
// one event loop drives all range requests concurrently (no extra threads).
// Returns the number of pieces whose downloaded size matched the request.
// When allow_slow_abort is set and NO range makes progress for a sustained
// window, all range requests are aborted so the caller can try the next source.
// 'pr' (nullable) collects progress samples for the calling thread.
static int download_ranges(const char *url, const char *path, long size,
                           int npieces, long piece_len, long timeout_ms,
                           bool allow_slow_abort, ProgressRelay *pr,
                           bool *range_hostile) {
    const long long SLOW_GRACE_MS = 8000;
    const long long STALL_ABORT_MS = 30000;
    struct Piece {
        FILE *fp = nullptr;
        QNetworkReply *reply = nullptr;
        long long written = 0;
        long len = 0;
        bool ok = false;
    };

    std::vector<long> offs(npieces), lens(npieces);
    for (int p = 0; p < npieces; p++) {
        offs[p] = (long)p * piece_len;
        lens[p] = (p == npieces - 1) ? (size - offs[p]) : piece_len;
    }

    std::vector<std::shared_ptr<Piece>> pieces;
    pieces.reserve(npieces);
    int active = 0;
    for (int p = 0; p < npieces; p++) {
        char sp[2048];
        snprintf(sp, sizeof(sp), "%s.chunk.%d", path, p);
        auto piece = std::make_shared<Piece>();
        piece->len = lens[p];
        // Resume: reuse a chunk left complete by a previous source attempt so
        // a stalling/failing source never forces us to re-download progress.
        long long existing = 0;
        if (mc_path_exists(sp)) {
            FILE *ef = fopen(sp, "rb");
            if (ef) {
                fseek(ef, 0, SEEK_END);
                existing = ftell(ef);
                fclose(ef);
            }
            if (existing == piece->len) {
                piece->ok = true;
                pieces.push_back(piece);
                continue;
            }
        }
        piece->fp = fopen(sp, "wb");
        pieces.push_back(piece);
        if (piece->fp) active++;
    }
    if (active == 0) return npieces;  // every chunk already complete

    bmclapi_throttle(url);

    QEventLoop loop;
    QTimer timer;
    auto timed_out = std::make_shared<bool>(false);
    auto done_count = std::make_shared<int>(0);
    auto progress_total = std::make_shared<long long>(0);
    auto t_start = std::chrono::steady_clock::now();
    auto last_activity = std::make_shared<std::chrono::steady_clock::time_point>(t_start);
    // Set when a server answers a byte-range request with 200 (whole body)
    // instead of 206: the source ignores Range and its chunks are garbage.
    auto hostile = std::make_shared<bool>(false);
    std::vector<std::vector<QMetaObject::Connection>> conns(npieces);

    for (int p = 0; p < npieces; p++) {
        auto piece = pieces[p];
        if (!piece->fp) continue;
        QUrl qurl(QString::fromUtf8(url));
        QNetworkRequest req(qurl);
        setup_req(req, timeout_ms);
        req.setRawHeader("Range", QString("bytes=%1-%2").arg(offs[p]).arg(offs[p] + lens[p] - 1).toUtf8());
        QNetworkReply *reply = pick_nam()->get(req);
        piece->reply = reply;

        conns[p].push_back(QObject::connect(reply, &QNetworkReply::readyRead, [piece, progress_total, size, &timer, timeout_ms, last_activity, pr]() {
            QByteArray data = piece->reply->readAll();
            if (piece->fp) {
                piece->written += (long long)fwrite(data.constData(), 1, (size_t)data.size(), piece->fp);
            }
            *progress_total += (long long)data.size();
            if (pr) pr->report(*progress_total, size);
            *last_activity = std::chrono::steady_clock::now();
            if (timeout_ms > 0) timer.start((int)timeout_ms);
        }));
        conns[p].push_back(QObject::connect(reply, &QNetworkReply::finished, [piece, &loop, done_count, active, timed_out, hostile]() {
            QByteArray data = piece->reply->readAll();
            if (piece->fp) {
                piece->written += (long long)fwrite(data.constData(), 1, (size_t)data.size(), piece->fp);
            }
            int status = piece->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200 && piece->len > 0)
                *hostile = true;   // whole body in response to a range request
            if (!*timed_out && piece->reply->error() == QNetworkReply::NoError &&
                (status == 200 || status == 206) && piece->written == piece->len)
                piece->ok = true;
            if (++(*done_count) == active || *hostile)
                loop.quit();
        }));
    }

    if (timeout_ms > 0) {
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, [&loop, timed_out]() {
            *timed_out = true;
            loop.quit();
        });
        timer.start((int)timeout_ms);
    }

    auto slow_abort = std::make_shared<bool>(false);
    QTimer slow_timer;
    QMetaObject::Connection swc;
    {
        slow_timer.setInterval(1000);
        swc = QObject::connect(&slow_timer, &QTimer::timeout,
                               [&loop, &pieces, timed_out, done_count, active, slow_abort,
                                last_activity, t_start, allow_slow_abort]() {
            if (*timed_out || *done_count >= active || !allow_slow_abort) return;
            auto now = std::chrono::steady_clock::now();
            long long idle_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - *last_activity).count();
            long long tot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t_start).count();
            // Abort only when NO range makes progress for a sustained window
            // (PCL BlockTimeout behavior). Slow-but-flowing sources survive.
            if (tot_ms > SLOW_GRACE_MS && idle_ms >= STALL_ABORT_MS) {
                *slow_abort = true;
                loop.quit();
            }
        });
        slow_timer.start();
    }

    QTimer cancel_timer;
    QMetaObject::Connection cc;
    {
        cancel_timer.setInterval(100);
        cc = QObject::connect(&cancel_timer, &QTimer::timeout, [&loop, timed_out]() {
            if (g_cancel) {
                *timed_out = true;
                loop.quit();
            }
        });
        cancel_timer.start();
    }

    loop.exec();
    timer.stop();
    {
        slow_timer.stop();
        QObject::disconnect(swc);
        cancel_timer.stop();
        QObject::disconnect(cc);
    }

    if (*slow_abort)
        mc_info("[DL-Q] slow source aborted: %s", path);
    if (*hostile && range_hostile)
        *range_hostile = true;

    int ok_count = 0;
    for (int p = 0; p < npieces; p++) {
        auto &piece = pieces[p];
        for (auto &c : conns[p]) QObject::disconnect(c);
        if (piece->fp) { fclose(piece->fp); piece->fp = nullptr; }
        if (piece->ok) ok_count++;
        else {
            if (piece->reply) piece->reply->abort();
            char sp[2048];
            snprintf(sp, sizeof(sp), "%s.chunk.%d", path, p);
            QFile::remove(QString::fromUtf8(sp));
        }
        if (piece->reply) piece->reply->deleteLater();
    }
    return ok_count;
}

// Download one file across its ordered sources on a pool thread.
// Returns 1 on success. 'pr' (nullable) collects progress samples.
static int download_one_file(const McQtBatchItem *it, long timeout_ms, ProgressRelay *pr) {
    const long long THRESH = 1024LL * 1024;
    if (!it->urls || it->url_count < 1 || !it->path || !it->path[0]) return 0;

    auto t0 = std::chrono::steady_clock::now();
    char dir[1024];
    mc_path_dirname(it->path, dir, sizeof(dir));
    mc_path_mkdir_p(dir);

    // Skip files that are already present and valid (re-install / resume):
    // existing file with size > 0 and matching SHA-1 needs no re-download.
    if (mc_path_exists(it->path)) {
        FILE *ef = fopen(it->path, "rb");
        if (ef) {
            fseek(ef, 0, SEEK_END);
            long long existing = ftell(ef);
            fclose(ef);
            if (existing > 0) {
                if (it->sha1 && it->sha1[0]) {
                    char as[64];
                    if (mc_hash_file_sha1(it->path, as, sizeof(as)) &&
                        mc_stricmp(as, it->sha1) == 0) {
                        mc_info("[DL-Q] skip existing (sha1 ok) %s", it->path);
                        return 1;
                    }
                } else if (it->size <= 0 || existing == it->size) {
                    mc_info("[DL-Q] skip existing (%lldB) %s", existing, it->path);
                    return 1;
                }
            }
        }
    }

    int succeeded = 0;
    int any_ranges = 0;

    // Try sources in order; each failed source only costs this file.
    for (int s = 0; s < it->url_count && !succeeded; s++) {
        const char *url = it->urls[s];
        if (!url || !*url) continue;

        // BMCLAPI mirrors ignore Range (they return the whole file), so a
        // single whole-file GET is the right shape for them.
        int use_ranges = (it->size > THRESH && g_max_pieces > 1 &&
                          !strstr(url, "bmclapi"));
        if (use_ranges) any_ranges = 1;

        // Adaptive timeout: grow as sources keep failing, capped at 30s.
        // A flaky mirror must not stall the file for the full budget.
        long eff_timeout = timeout_ms;
        if (s > 0) eff_timeout = std::min(30000L, std::max(timeout_ms, 15000L * (1L + s)));
        if (strstr(url, "bmclapi") && eff_timeout > 10000L) eff_timeout = 10000L;

        if (use_ranges) {
            int npieces = (int)(it->size / THRESH) + 1;
            if (npieces > g_max_pieces) npieces = g_max_pieces;
            if (npieces > g_thread_limit) npieces = g_thread_limit;
            if (npieces < 1) npieces = 1;
            long piece_len = it->size / npieces;
            if (piece_len < 256 * 1024) {
                piece_len = 256 * 1024;
                npieces = (int)((it->size + piece_len - 1) / piece_len);
                if (npieces > 8) npieces = 8;
            }
            mc_info("[DL-Q] file %s: %d pieces x %ldB", it->path, npieces, piece_len);

            // NOTE: no clean_temp() here — valid chunks are reused across
            // sources so a stalling mirror never costs already-fetched bytes.
            bool range_hostile = false;
            int got = download_ranges(url, it->path, it->size, npieces, piece_len, eff_timeout,
                                      s < it->url_count - 1, pr, &range_hostile);
            mc_info("[DL-Q]   pieces ok %d/%d%s", got, npieces, range_hostile ? " (range-hostile)" : "");
            if (got == npieces && merge_file(it->path, it->size, it->sha1)) {
                succeeded = 1;
            } else {
                // A range-hostile source wrote whole-file garbage into the
                // chunk files; wipe them. Otherwise keep good chunks for resume.
                if (range_hostile)
                    clean_temp(it->path);
            }
        } else {
            // Small/unknown-size file: single GET, verify size/hash.
            // Whole-file supersedes chunk resume, so drop any stale chunks.
            clean_temp(it->path);
            long expect = (it->size > 0) ? it->size : -1;
            if (download_piece(url, it->path, -1, expect, eff_timeout,
                               s < it->url_count - 1, pr) != 0)
                continue;
            int ok = 1;
            if (it->sha1 && it->sha1[0]) {
                char as[64];
                if (!mc_hash_file_sha1(it->path, as, sizeof(as)) || mc_stricmp(as, it->sha1) != 0)
                    ok = 0;
            }
            if (ok) succeeded = 1;
        }
    }

    // Range path failed on every source: fall back to a whole-file GET.
    if (!succeeded && any_ranges) {
        mc_info("[DL-Q]   range path failed, whole-file fallback for %s", it->path);
        for (int s = 0; s < it->url_count && !succeeded; s++) {
            const char *url = it->urls[s];
            if (!url || !*url) continue;
            long eff_timeout = timeout_ms;
            if (s > 0) eff_timeout = std::min(30000L, std::max(timeout_ms, 15000L * (1L + s)));
            if (strstr(url, "bmclapi") && eff_timeout > 10000L) eff_timeout = 10000L;
            clean_temp(it->path);
            if (download_piece(url, it->path, -1, -1, eff_timeout,
                               s < it->url_count - 1, pr) != 0)
                continue;
            int ok = 1;
            if (it->sha1 && it->sha1[0]) {
                char as[64];
                if (!mc_hash_file_sha1(it->path, as, sizeof(as)) || mc_stricmp(as, it->sha1) != 0)
                    ok = 0;
            }
            if (ok) succeeded = 1;
        }
    }

    if (succeeded) {
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t0).count();
        if (ms > 1500)
            mc_info("[DL-Q] slow file %s: %lldms size=%ld", it->path, ms, it->size);
        return 1;
    }
    clean_temp(it->path);
    return 0;
}

// ---- Public API ----

int mc_qt_download_file(const char *url, const char *output_path,
                        const char *expected_sha1, long expected_size,
                        long timeout_ms) {
    if (!url || !*url || !output_path || !*output_path) return 0;
    auto fut = pool_submit([=]() {
        const char *urls[1] = { url };
        McQtBatchItem it{ urls, 1, output_path, expected_sha1, expected_size };
        return download_one_file(&it, timeout_ms, nullptr);
    });
    pool_wait(fut);
    return fut.get();
}

int mc_qt_download_file_progress(const char *url, const char *output_path,
                                  const char *expected_sha1, long expected_size,
                                  long timeout_ms,
                                  McDownloadProgressFn progress, void *userdata) {
    Q_UNUSED(progress);
    Q_UNUSED(userdata);
    return mc_qt_download_file(url, output_path, expected_sha1, expected_size, timeout_ms);
}

int mc_qt_download_file_multi(const char **urls, int url_count,
                               const char *output_path,
                               const char *expected_sha1, long expected_size,
                               long timeout_ms) {
    if (!urls || url_count <= 0 || !output_path || !*output_path) return 0;
    auto fut = pool_submit([=]() {
        McQtBatchItem it{ urls, url_count, output_path, expected_sha1, expected_size };
        return download_one_file(&it, timeout_ms, nullptr);
    });
    pool_wait(fut);
    return fut.get();
}

int mc_qt_download_multi_progress(const char **urls, int url_count,
                                  const char *output_path,
                                  const char *expected_sha1, long expected_size,
                                  long timeout_ms,
                                  McQtDownloadProgressFn progress, void *userdata) {
    if (!urls || url_count <= 0 || !output_path || !*output_path) return 0;
    auto relay = std::make_shared<ProgressRelay>();
    pool_submit([=]() {
        McQtBatchItem it{ urls, url_count, output_path, expected_sha1, expected_size };
        int ok = download_one_file(&it, timeout_ms, relay.get());
        relay->finish(ok);
        return ok;
    });
    return relay_wait(*relay, progress, userdata);
}

int mc_qt_download_batch(const char **urls, const char **paths,
                          const char **sha1s, const long *sizes,
                          int count, long timeout_ms,
                          int results[])
{
    if (count <= 0) return 0;
    if (results) memset(results, 0, (size_t)count * sizeof(int));

    std::vector<std::future<int>> futs;
    futs.reserve((size_t)count);
    for (int i = 0; i < count; i++) {
        const char *u = urls ? urls[i] : nullptr;
        const char *p = paths ? paths[i] : nullptr;
        if (!u || !*u || !p || !*p) {
            if (results) results[i] = 0;
            continue;
        }
        const char *sha1 = (sha1s && sha1s[i]) ? sha1s[i] : nullptr;
        long sz = sizes ? sizes[i] : 0L;
        futs.push_back(pool_submit([=]() {
            const char *uu[1] = { u };
            McQtBatchItem it{ uu, 1, p, sha1, sz };
            return download_one_file(&it, timeout_ms, nullptr);
        }));
    }

    int ok = 0;
    for (size_t k = 0; k < futs.size(); k++) {
        pool_wait(futs[k]);
        int r = futs[k].get();
        if (r) ok++;
        if (results) results[k] = r;
    }
    return ok;
}

int mc_qt_download_batch_ex(const McQtBatchItem *items, int count,
                            long timeout_ms, int results[]) {
    if (count <= 0) return 0;
    if (results) memset(results, 0, (size_t)count * sizeof(int));

    std::vector<int> order(count);
    for (int i = 0; i < count; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [items](int a, int b) {
        return items[a].size > items[b].size;
    });

    std::atomic<int> ok_count{0};
    auto t_batch = std::chrono::steady_clock::now();
    std::vector<std::future<int>> futs;
    futs.reserve((size_t)count);
    for (int k = 0; k < count; k++) {
        int idx = order[k];
        const McQtBatchItem *src = &items[idx];
        futs.push_back(pool_submit([src, timeout_ms, &ok_count]() {
            if (g_cancel) return 0;
            int ok = download_one_file(src, timeout_ms, nullptr);
            if (ok) ok_count++;
            return ok;
        }));
    }

    for (auto &f : futs) pool_wait(f);

    for (int k = 0; k < count; k++) {
        int idx = order[k];
        if (results) results[idx] = futs[k].get() ? 1 : 0;
    }

    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t_batch).count();
    mc_info("[DL-Q]   batch finished: %d/%d files ok (%lldms)", ok_count.load(), count, ms);
    return ok_count.load();
}
