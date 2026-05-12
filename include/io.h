/**
 * @file io.h
 * @brief Image write and I/O pipeline
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <windows.h>
#include "bufus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Progress callback and I/O parameters                               */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Progress callback function signature.
 *
 * Called periodically during write/verify operations to report progress.
 *
 * @param done       Bytes completed so far
 * @param total      Total bytes to process
 * @param speed_mbs  Current speed in MiB/s
 * @param userdata   Opaque pointer passed from caller
 */
typedef void (*progress_callback_t)(uint64_t done,
                                    uint64_t total,
                                    double   speed_mbs,
                                    void    *userdata);

/**
 * Parameters for image write operation.
 */
typedef struct {
    HANDLE              src;            /**< Source file handle (buffered)   */
    HANDLE              dst;            /**< Destination device handle       */
    uint64_t            src_size;       /**< Source file size in bytes       */
    uint32_t            block_size;     /**< I/O block size in bytes         */
    uint32_t            sector_size;    /**< Device sector size in bytes     */
    progress_callback_t progress;       /**< Optional progress callback      */
    void               *userdata;       /**< Opaque data for callback        */
} io_params_t;

/* ───────────────────────────────────────────────────────────────── */
/* File and I/O operations                                            */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Open a source file for reading.
 *
 * Opens an image file (ISO, IMG) with buffered I/O for sequential reading.
 * Uses FILE_FLAG_SEQUENTIAL_SCAN hint for prefetching optimization.
 *
 * @param path        File path to open
 * @param out         Pointer to receive file handle (non-NULL)
 * @param out_size    Pointer to receive file size in bytes (non-NULL)
 *
 * @return BUFUS_OK on success, BUFUS_ERR_OPEN or BUFUS_ERR_IO_READ on failure
 */
bufus_err_t io_open_source(const char *path, HANDLE *out, uint64_t *out_size);

/**
 * Write an image to a device with sector-aligned buffering.
 *
 * Reads blocks from source (buffered, any size), zero-pads to sector
 * boundary, then writes to device with NO_BUFFERING. Allows reading
 * an ISO file smaller than the destination drive.
 *
 * Calls progress callback periodically for status updates.
 *
 * @param p Parameters structure (non-NULL)
 *
 * @return BUFUS_OK on success, various error codes on failure
 */
bufus_err_t io_write_image(const io_params_t *p);

#ifdef __cplusplus
}
#endif

#endif /* IO_H */
