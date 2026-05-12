#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "logger.h"

/* ── Internal state ───────────────────────────────────────────────── */
static struct {
    FILE       *file;
    log_level_t min_level;
    HANDLE      mutex;
} g_log = {NULL, LOG_INFO, NULL};

static const char *level_tag[]  = {"DEBUG", "INFO ", "WARN ", "ERROR"};

/* Win32 console attribute per level */
static const WORD level_attr[] = {
    FOREGROUND_INTENSITY,                                              /* DEBUG: grey    */
    FOREGROUND_GREEN | FOREGROUND_INTENSITY,                           /* INFO:  green   */
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,          /* WARN:  yellow  */
    FOREGROUND_RED | FOREGROUND_INTENSITY,                             /* ERROR: red     */
};

#define COL_RESET (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

/* ── Public API ───────────────────────────────────────────────────── */

void log_init(const char *path, bool verbose) {
    g_log.min_level = verbose ? LOG_DEBUG : LOG_INFO;
    g_log.mutex     = CreateMutexA(NULL, FALSE, NULL);

    if (path && path[0] != '\0') {
        fopen_s(&g_log.file, path, "a");
        if (!g_log.file) {
            fprintf(stderr, "[logger] Warning: could not open log file '%s'\n", path);
        }
    }
}

void log_close(void) {
    if (g_log.file) {
        fflush(g_log.file);
        fclose(g_log.file);
        g_log.file = NULL;
    }
    if (g_log.mutex) {
        CloseHandle(g_log.mutex);
        g_log.mutex = NULL;
    }
}

void log_write(log_level_t lvl, const char *fmt, ...) {
    if (lvl < g_log.min_level) return;

    /* Format message */
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Timestamp */
    SYSTEMTIME st;
    GetLocalTime(&st);

    WaitForSingleObject(g_log.mutex, INFINITE);

    /* Coloured console output */
    HANDLE hcon = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hcon, level_attr[lvl]);
    fprintf(stderr, "[%02d:%02d:%02d] [%s] %s\n",
            st.wHour, st.wMinute, st.wSecond, level_tag[lvl], msg);
    SetConsoleTextAttribute(hcon, COL_RESET);

    /* Optional file output (plain text, no colour codes) */
    if (g_log.file) {
        fprintf(g_log.file, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond,
                level_tag[lvl], msg);
        fflush(g_log.file);
    }

    ReleaseMutex(g_log.mutex);
}