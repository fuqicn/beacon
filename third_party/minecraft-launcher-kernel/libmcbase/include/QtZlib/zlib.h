/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 *
 * Compatibility shim for <QtZlib/zlib.h>. Newer Qt releases (e.g. Qt 6.11)
 * no longer ship a QtZlib module header; fall back to the system zlib.
 */
#ifndef QTZLIB_COMPAT_ZLIB_H
#define QTZLIB_COMPAT_ZLIB_H

#include <cstdlib>
#include <zlib.h>

#endif
