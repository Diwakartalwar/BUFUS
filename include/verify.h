/**
 * @file verify.h
 * @brief Image verification and integrity checking
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef VERIFY_H
#define VERIFY_H

#include <stdint.h>
#include <windows.h>
#include "io.h"
#include "bufus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Verification parameters                                            */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Parameters for image verification operation.
 */
typedef struct {
    HANDLE              src;            /**< Source file handle (buffered)   */
    HANDLE              dst;            /**< Device handle (NO_BUFFERING)    */
    uint64_t            size;           /**< Size to verify in bytes         */
    uint32_t            block_size;     /**< I/O block size in bytes         */
    uint32_t            sector_size;    /**< Device sector size in bytes     */
    progress_callback_t progress;       /**< Optional progress callback      */
    void               *userdata;       /**< Opaque data for callback        */
} verify_params_t;

/* ───────────────────────────────────────────────────────────────── */
/* Verification operations                                            */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Verify that written data matches source file.
 *
 * Reads the source file (buffered) and corresponding device sectors,
 * compares byte-for-byte up to the specified size. The device is read
 * in sector-aligned blocks, but only source bytes are compared (pad
 * bytes are ignored).
 *
 * Calls progress callback periodically for status updates.
 *
 * @param p Parameters structure (non-NULL)
 *
 * @return BUFUS_OK if all data matches, BUFUS_ERR_VERIFY if mismatch,
 *         other error codes for I/O failures
 */
bufus_err_t verify_image(const verify_params_t *p);

#ifdef __cplusplus
}
#endif

#endif /* VERIFY_H */
