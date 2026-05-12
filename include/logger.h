#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

/* ── Log Levels ─────────────────────────────────────────────────── */

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

/* ── Public API ─────────────────────────────────────────────────── */

void log_init(const char *path, bool verbose);
void log_close(void);

void log_write(log_level_t lvl, const char *fmt, ...);

/* ── Logging Macros ─────────────────────────────────────────────── */
/*
 * ##__VA_ARGS__ removes the extra comma when no variadic
 * arguments are provided.
 *
 * Example:
 *   LOGI("hello");
 * expands correctly.
 */

#define LOGD(fmt, ...) log_write(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) log_write(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) log_write(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) log_write(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif // LOGGER_H