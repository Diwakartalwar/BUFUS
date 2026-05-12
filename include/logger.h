/**
 * @file logger.h
 * @brief Logging utilities with console and file output
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Log levels                                                         */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Logging severity levels.
 */
typedef enum {
    LOG_DEBUG = 0,      /**< Debug-level diagnostic information  */
    LOG_INFO  = 1,      /**< General informational messages      */
    LOG_WARN  = 2,      /**< Warning messages                    */
    LOG_ERROR = 3,      /**< Error messages                      */
} log_level_t;

/* ───────────────────────────────────────────────────────────────── */
/* Logger initialization and control                                  */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Initialize the logging system.
 *
 * Sets up the logger for console and optional file output. Console
 * output includes ANSI color codes via Windows console API. File
 * output is plain text with timestamps.
 *
 * @param path    Optional log file path (NULL or empty string for stderr only)
 * @param verbose If true, set min level to DEBUG; else INFO
 */
void log_init(const char *path, bool verbose);

/**
 * Close the logging system.
 *
 * Flushes and closes the file handle if a log file was opened.
 * Safe to call multiple times or if log_init was never called.
 */
void log_close(void);

/* ───────────────────────────────────────────────────────────────── */
/* Logging functions                                                  */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Write a log message.
 *
 * Formats and outputs a message to stderr (with color) and optionally
 * to the log file (plain text). Thread-safe via internal mutex.
 *
 * Messages below the current min level are ignored.
 *
 * @param lvl   Log level
 * @param fmt   printf-style format string
 * @param ...   Format arguments
 */
void log_write(log_level_t lvl, const char *fmt, ...);

/* ───────────────────────────────────────────────────────────────── */
/* Convenience macros                                                 */
/* ───────────────────────────────────────────────────────────────── */

/** Log a debug message (only if verbose mode) */
#define LOGD(fmt, ...) log_write(LOG_DEBUG, fmt, __VA_ARGS__)

/** Log an informational message */
#define LOGI(fmt, ...) log_write(LOG_INFO, fmt, __VA_ARGS__)

/** Log a warning message */
#define LOGW(fmt, ...) log_write(LOG_WARN, fmt, __VA_ARGS__)

/** Log an error message */
#define LOGE(fmt, ...) log_write(LOG_ERROR, fmt, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
