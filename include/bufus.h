/**
 * @file bufus.h
 * @brief BUFUS main header - core types, enums, and declarations
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef BUFUS_H
#define BUFUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Error type and codes                                              */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Error codes for BUFUS operations.
 */
typedef enum {
    BUFUS_OK            = 0,   /**< Success                          */
    BUFUS_ERR_ARGS      = 1,   /**< Invalid arguments                */
    BUFUS_ERR_PRIVILEGE = 2,   /**< Insufficient privileges          */
    BUFUS_ERR_NOT_FOUND = 3,   /**< Device or file not found         */
    BUFUS_ERR_OPEN      = 4,   /**< Cannot open device or file       */
    BUFUS_ERR_LOCK      = 5,   /**< Cannot lock volume               */
    BUFUS_ERR_IO_READ   = 6,   /**< Read error                       */
    BUFUS_ERR_IO_WRITE  = 7,   /**< Write error                      */
    BUFUS_ERR_VERIFY    = 8,   /**< Verification failed              */
    BUFUS_ERR_NOMEM     = 9,   /**< Out of memory                    */
    BUFUS_ERR_WIN32     = 10,  /**< Unexpected Win32 API error       */
} bufus_err_t;

/* ───────────────────────────────────────────────────────────────── */
/* Configuration and constants                                        */
/* ───────────────────────────────────────────────────────────────── */

#define BUFUS_MAX_DRIVES        32      /**< Maximum physical drives to detect   */
#define BUFUS_MAX_PATH_LEN      260     /**< Maximum path length (Windows MAX_PATH) */
#define BUFUS_DEFAULT_BLOCK     (4 * 1024 * 1024)  /**< Default I/O block size (4 MiB) */

/**
 * BUFUS configuration structure passed to the application.
 */
typedef struct {
    char     source[BUFUS_MAX_PATH_LEN];    /**< Source image file path          */
    char     log_file[BUFUS_MAX_PATH_LEN];  /**< Optional log file path          */
    int      drive_index;                   /**< Target physical drive index     */
    uint32_t block_size;                    /**< I/O block size in bytes         */
    bool     verify;                        /**< Verify after write              */
    bool     force;                         /**< Skip confirmation prompt        */
    bool     verbose;                       /**< Enable debug logging            */
    bool     benchmark;                     /**< Run benchmark instead of write  */
} bufus_cfg_t;

/* ───────────────────────────────────────────────────────────────── */
/* Error handling                                                     */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Convert error code to human-readable string.
 *
 * @param e Error code
 * @return Pointer to static error message string
 */
const char *bufus_err_str(bufus_err_t e);

/**
 * Check if current process has administrator privileges.
 *
 * @return true if elevated, false otherwise
 */
bool bufus_is_elevated(void);

#ifdef __cplusplus
}
#endif

#endif /* BUFUS_H */
