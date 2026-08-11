/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#include "mc_log.h"
#include <iostream>
#include <fstream>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static McLogLevel g_level = MC_LOG_INFO;
static std::ofstream g_file;
static std::mutex g_log_mtx;
static int g_progress_active = 0;
static McOutputMode g_output_mode = MC_OUTPUT_HUMAN;

static const char *level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

void mc_log_set_level(McLogLevel level) { g_level = level; }

void mc_log_set_progress(int active) {
    std::lock_guard<std::mutex> lock(g_log_mtx);
    g_progress_active = active;
}

void mc_log_set_file(const char *path) {
    if (g_file.is_open()) g_file.close();
    if (path) g_file.open(path, std::ios::app);
}

void mc_output_set_mode(McOutputMode mode) {
    g_output_mode = mode;
}

McOutputMode mc_output_get_mode(void) {
    return g_output_mode;
}

// Custom streambuf that routes cout through WriteConsoleW on Windows
// for correct UTF-8 output, while the rest of the code uses std::cout.
class ConsoleBuf : public std::streambuf {
    char m_buf[4096];
protected:
    int overflow(int c) override {
        if (c != EOF) {
            *pptr() = (char)c;
            pbump(1);
        }
        if (c == EOF || pptr() >= epptr())
            return sync() == -1 ? EOF : c;
        return c;
    }
    int sync() override {
        std::ptrdiff_t n = pptr() - pbase();
        if (n <= 0) return 0;
#ifdef _WIN32
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode;
        if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, pbase(), (int)n, nullptr, 0);
            if (wlen > 0) {
                wchar_t *wbuf = (wchar_t*)malloc(wlen * sizeof(wchar_t));
                if (wbuf) {
                    MultiByteToWideChar(CP_UTF8, 0, pbase(), (int)n, wbuf, wlen);
                    WriteConsoleW(h, wbuf, wlen, nullptr, nullptr);
                    free(wbuf);
                    setp(m_buf, m_buf + sizeof(m_buf));
                    return 0;
                }
            }
        }
        // Fallback: piped or unable to convert
        fwrite(pbase(), 1, (size_t)n, stdout);
        fflush(stdout);
#else
        fwrite(pbase(), 1, (size_t)n, stdout);
        fflush(stdout);
#endif
        setp(m_buf, m_buf + sizeof(m_buf));
        return 0;
    }
public:
    ConsoleBuf() { setp(m_buf, m_buf + sizeof(m_buf)); }
};

static ConsoleBuf g_console_buf;

// JSON-escape a string: escape " \ \n \t and control chars.
// Returns allocated buffer; caller must free().
static char *json_escape(const char *s) {
    if (!s) return strdup("null");
    size_t len = strlen(s);
    char *out = (char*)malloc(len * 6 + 1);
    if (!out) return strdup(s);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out[j++] = '\\'; out[j++] = '"';  break;
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
            case '\t': out[j++] = '\\'; out[j++] = 't';  break;
            case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
            default:
                if (c < 0x20) {
                    j += snprintf(out + j, 7, "\\u%04x", c);
                } else {
                    out[j++] = c;
                }
                break;
        }
    }
    out[j] = '\0';
    return out;
}

static void write_console(const char *str) {
    std::cout << str << std::flush;
}

void mc_console_write(const char *str) {
    if (g_output_mode == MC_OUTPUT_JSON) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
        // Split multi-line strings into individual JSON lines
        const char *p = str;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > 0) {
                char *seg = (char*)malloc(len + 1);
                if (seg) {
                    memcpy(seg, p, len); seg[len] = '\0';
                    char *esc = json_escape(seg);
                    if (esc) {
                        std::cout << "{\"time\":\"" << timebuf << "\",\"level\":\"DATA\",\"msg\":\"" << esc << "\"}\n" << std::flush;
                        free(esc);
                    }
                    free(seg);
                }
            }
            if (nl) p = nl + 1; else break;
        }
    } else {
        write_console(str);
    }
}

void mc_console_init(void) {
    std::cout.rdbuf(&g_console_buf);
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void mc_console_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (g_output_mode == MC_OUTPUT_JSON) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
        char *esc = json_escape(buf);
        if (esc) {
            std::cout << "{\"time\":\"" << timebuf << "\",\"level\":\"DATA\",\"msg\":\"" << esc << "\"}\n" << std::flush;
            free(esc);
        }
    } else {
        std::cout << buf << std::flush;
    }
}

void mc_log(McLogLevel level, const char *fmt, ...) {
    if (level < g_level) return;
    std::lock_guard<std::mutex> lock(g_log_mtx);
    if (g_progress_active)
        std::cout << "\r                                                                                \r" << std::flush;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
    const char *lvl = (level >= 0 && level <= 3) ? level_names[level] : "?";
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_output_mode == MC_OUTPUT_JSON) {
        char *escaped = json_escape(buf);
        if (escaped) {
            char line[4160];
            snprintf(line, sizeof(line), "{\"time\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"}\n", timebuf, lvl, escaped);
            std::cout << line << std::flush;
            free(escaped);
        }
    } else {
        char line[4160];
        snprintf(line, sizeof(line), "[%s] %s: %s\n", timebuf, lvl, buf);
        std::cout << line << std::flush;
    }
    if (g_file.is_open()) {
        g_file << "[" << timebuf << "] " << lvl << ": " << buf << std::endl;
    }
}
