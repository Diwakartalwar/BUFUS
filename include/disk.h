/**
 * @file disk.h
 * @brief Disk geometry and raw sector operations
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include <windows.h>
#include "bufus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Disk geometry structures                                           */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Disk geometry and capacity information.
 */
typedef struct {
    uint64_t size_bytes;        /**< Total capacity in bytes          */
    uint32_t sector_size;       /**< Sector size in bytes             */
    uint64_t total_sectors;     /**< Total number of sectors          */
} disk_geometry_t;

/* ───────────────────────────────────────────────────────────────── */
/* Disk I/O operations                                                */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Query disk geometry via IOCTL.
 *
 * Retrieves disk size, sector size, and total sector count using
 * IOCTL_DISK_GET_DRIVE_GEOMETRY_EX.
 *
 * @param h   Device handle (from device_open)
 * @param out Pointer to disk_geometry_t output (non-NULL)
 *
 * @return BUFUS_OK on success, BUFUS_ERR_WIN32 on IOCTL failure
 */
bufus_err_t disk_get_geometry(HANDLE h, disk_geometry_t *out);

/**
 * Wipe the Master Boot Record (first sector).
 *
 * Writes zeros to the first sector of the disk. Does not affect the
 * remainder of the disk. Uses VirtualAlloc for sector-aligned buffer.
 *
 * @param h             Device handle (from device_open)
 * @param sector_size   Sector size in bytes (0 defaults to 512)
 *
 * @return BUFUS_OK on success, BUFUS_ERR_NOMEM or BUFUS_ERR_IO_WRITE on failure
 */
bufus_err_t disk_wipe_mbr(HANDLE h, uint32_t sector_size);

/**
 * Sanitize stale partition metadata before imaging.
 *
 * Zeros a region at the start of disk (MBR/GPT primary area) and a region
 * at the end of disk (GPT backup header/table) so old layouts cannot survive
 * when writing a smaller image to a larger USB.
 *
 * @param h           Device handle
 * @param disk_size   Total disk size in bytes
 * @param image_size  Incoming image size in bytes
 *
 * @return BUFUS_OK on success, otherwise write/readiness error
 */
bufus_err_t disk_sanitize_layout(HANDLE h, uint64_t disk_size, uint64_t image_size);

/**
 * Ask Windows storage stack to refresh cached disk layout.
 *
 * @param h Device handle
 * @return BUFUS_OK on success, BUFUS_ERR_WIN32 on IOCTL failure
 */
bufus_err_t disk_refresh_layout(HANDLE h);

#ifdef __cplusplus
}
#endif

#endif /* DISK_H */
