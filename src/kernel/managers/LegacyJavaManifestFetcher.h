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
#ifndef LEGACY_JAVA_MANIFEST_FETCHER_H
#define LEGACY_JAVA_MANIFEST_FETCHER_H

#include "mc_java_dl.h"

// Legacy concurrent Java manifest fetch (pre-fix version). Spawns a bare
// std::thread per candidate source; each thread runs mc_http_get which embeds
// a nested QEventLoop::exec() over a thread-local QNetworkAccessManager. Those
// bare threads compete with the main GUI event loop over Qt's global
// (non-thread-safe) event dispatcher, which is the historical root cause of
// the Java-download UI freeze.
//
// This is NOT used by the normal download path. It is kept here so the manual
// memory-optimization tool can reproduce the exact stall by invoking it on a
// worker thread and cancelling as soon as the file count is known. The freeze
// ends the moment the manifest returns (file count known).
int mc_java_download_manifest_legacy(int major_version, const char *mirror, McJavaFileList *list);

#endif
