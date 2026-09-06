/*
 * MIT License
 *
 * Copyright (c) 2026 fuqicn
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MC_LOG_H
#define MC_LOG_H

typedef enum {
    MC_LOG_DEBUG,
    MC_LOG_INFO,
    MC_LOG_WARN,
    MC_LOG_ERROR,
    MC_LOG_NONE
} McLogLevel;

typedef enum {
    MC_OUTPUT_HUMAN = 0,
    MC_OUTPUT_JSON  = 1
} McOutputMode;

void mc_log_set_level(McLogLevel level);
void mc_log_set_file(const char *path);
void mc_log_set_progress(int active);
void mc_log(McLogLevel level, const char *fmt, ...);

#define mc_debug(fmt, ...)  mc_log(MC_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define mc_info(fmt, ...)   mc_log(MC_LOG_INFO,  fmt, ##__VA_ARGS__)
#define mc_warn(fmt, ...)   mc_log(MC_LOG_WARN,  fmt, ##__VA_ARGS__)
#define mc_error(fmt, ...)  mc_log(MC_LOG_ERROR, fmt, ##__VA_ARGS__)

// Output mode: HUMAN (default) or JSON
void mc_output_set_mode(McOutputMode mode);
McOutputMode mc_output_get_mode(void);

// Write UTF-8 string to console via WriteConsoleW (proper Unicode)
void mc_console_write(const char *str);
void mc_console_printf(const char *fmt, ...);

// Initialize console for UTF-8 output (cross-platform)
void mc_console_init(void);

#endif
