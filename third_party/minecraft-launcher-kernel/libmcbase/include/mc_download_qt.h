/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_DOWNLOAD_QT_H
#define MC_DOWNLOAD_QT_H

#include <stddef.h>

typedef void (*McDownloadProgressFn)(long long received, long long total, void *userdata);

// Initialize global shared QNetworkAccessManager (must be called from main thread).
void mc_qt_download_init(void);
// Cleanup global shared QNetworkAccessManager.
void mc_qt_download_cleanup(void);

// Thread-safe single file download using QtNetwork internals.
// Routes to main thread for shared QNAM connection reuse (PCLCE IHttpClientFactory pattern).
int mc_qt_download_file(const char *url, const char *output_path,
                        const char *expected_sha1, long expected_size,
                        long timeout_ms);

// Variant with progress callback. Called periodically with received/total bytes.
int mc_qt_download_file_progress(const char *url, const char *output_path,
                                  const char *expected_sha1, long expected_size,
                                  long timeout_ms,
                                  McDownloadProgressFn progress, void *userdata);

// Download a file trying multiple URLs in parallel.
int mc_qt_download_file_multi(const char **urls, int url_count,
                               const char *output_path,
                               const char *expected_sha1, long expected_size,
                               long timeout_ms);

// Per-file progress callback used by the multi-URL downloaders below.
// 'received'/'total' are bytes (total < 0 when unknown). Called periodically
// from the calling thread; do not block in it.
typedef void (*McQtDownloadProgressFn)(const char *path, long long received,
                                       long long total, void *userdata);

// Download one file across ordered sources (mirror first, official fallback)
// with live byte progress. Blocks the calling thread; safe off the GUI thread.
int mc_qt_download_multi_progress(const char **urls, int url_count,
                                  const char *output_path,
                                  const char *expected_sha1, long expected_size,
                                  long timeout_ms,
                                  McQtDownloadProgressFn progress, void *userdata);

// Pre-resolve DNS for common Minecraft hosts.
void mc_qt_dns_prefetch(void);

// Batch submit: enqueue all files at once, wait for all.
// URLs/paths/sizes must have 'count' elements. sha1s may be NULL.
// results (size count) receives 1/0 per file. Returns total success count.
int mc_qt_download_batch(const char **urls, const char **paths,
                          const char **sha1s, const long *sizes,
                          int count, long timeout_ms,
                          int results[]);

// ---------------------------------------------------------------------------
// PCL-style batch engine (per-file ordered sources + parallel range download).
// ---------------------------------------------------------------------------

// One file to download. 'urls' is an ordered source list (e.g. mirror first,
// official fallback). Sources are tried per-file: on failure the engine moves
// to the next source for that file only (never re-downloads the whole batch).
typedef struct McQtBatchItem {
    const char *const *urls;  // ordered source URLs
    int url_count;
    const char *path;         // output path (directories created as needed)
    const char *sha1;         // expected SHA-1, may be NULL
    long size;                // expected size; 0 = unknown
} McQtBatchItem;

// Batch download with per-file ordered sources. Files larger than 1MB are
// split into ranges downloaded in parallel across a worker pool. On failure a
// file falls back to a single-thread full download across all sources.
// results[] (count entries) receives 1/0 per file. Returns total success count.
int mc_qt_download_batch_ex(const McQtBatchItem *items, int count,
                            long timeout_ms, int results[]);

// Request cooperative cancellation of any in-flight downloads. When set,
// active request loops bail out promptly so callers can shut down without
// force-terminating worker threads. Reset to 0 before starting new work.
void mc_qt_download_set_cancel(int v);
int  mc_qt_download_cancel(void);

// Global concurrency controls (defaults: 8 worker threads, 4 pieces/file).
void mc_qt_download_set_thread_limit(int n);
int  mc_qt_download_thread_limit(void);
void mc_qt_download_set_max_pieces(int n);
int  mc_qt_download_max_pieces(void);

// Per-thread switch: when set, the calling thread never pumps the Qt event
// loop while waiting on the download pool (it blocks on the futures instead).
// Use on plain std::thread workers that share the pool with other threads;
// QCoreApplication::processEvents must not run concurrently from two threads.
void mc_qt_download_thread_no_pump(int on);

#endif
