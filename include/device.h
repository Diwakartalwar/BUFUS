/**
 * @file device.h
 * @brief Physical device enumeration and management
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#include "bufus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Device information structures                                      */
/* ───────────────────────────────────────────────────────────────── */

#define DEVICE_PATH_LEN    32       /**< Max length for device path         */
#define DEVICE_MODEL_LEN   128      /**< Max length for model string        */

/**
 * Information about a physical device.
 */
typedef struct {
    int      index;                 /**< PhysicalDrive index (0-based)    */
    uint64_t size_bytes;            /**< Device capacity in bytes         */
    uint32_t sector_size;           /**< Sector size in bytes             */
    char     path[DEVICE_PATH_LEN];  /**< Device path (e.g., "\\\\.\\PhysicalDrive0") */
    char     model[DEVICE_MODEL_LEN]; /**< Model/product string            */
    bool     is_removable;          /**< USB/removable media flag         */
} device_info_t;

/**
 * List of detected physical devices.
 */
typedef struct {
    device_info_t drives[BUFUS_MAX_DRIVES];  /**< Array of devices        */
    int           count;                     /**< Number of devices found */
} device_list_t;

/* ───────────────────────────────────────────────────────────────── */
/* Device enumeration                                                 */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Enumerate all physical drives on the system.
 *
 * Queries each PhysicalDrive0 through PhysicalDriveN to collect
 * geometry, capacity, and model information. Populates the device list.
 *
 * @param out Pointer to device_list_t output structure (non-NULL)
 * @return BUFUS_OK (always succeeds; count may be 0 if no drives found)
 */
bufus_err_t device_enumerate(device_list_t *out);

/**
 * Get information about a specific physical drive.
 *
 * Queries a single drive for geometry and identification data.
 *
 * @param index Index of the physical drive (0-based)
 * @param out   Pointer to device_info_t output (non-NULL)
 *
 * @return BUFUS_OK on success, BUFUS_ERR_NOT_FOUND if drive doesn't exist
 */
bufus_err_t device_get_info(int index, device_info_t *out);

/* ───────────────────────────────────────────────────────────────── */
/* Device access control                                              */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Open a physical drive with raw I/O flags.
 *
 * Opens the device with FILE_FLAG_NO_BUFFERING and FILE_FLAG_WRITE_THROUGH
 * to enable sector-aligned direct I/O. Requires administrator privileges.
 *
 * @param index         PhysicalDrive index
 * @param out_handle    Pointer to receive handle (non-NULL)
 *
 * @return BUFUS_OK on success, BUFUS_ERR_PRIVILEGE if not admin,
 *         BUFUS_ERR_OPEN if device cannot be opened
 */
bufus_err_t device_open(int index, HANDLE *out_handle);

/**
 * Lock all volumes on a physical drive and dismount them.
 *
 * Prevents other processes from accessing the drive and removes any
 * mounted filesystems. Necessary before writing to raw sectors.
 *
 * @param h     Device handle (from device_open)
 * @param index PhysicalDrive index (for volume enumeration)
 *
 * @return BUFUS_OK (best-effort; proceeds even if some locks fail)
 */
bufus_err_t device_lock(HANDLE h, int index);

/**
 * Unlock a device and request partition table refresh.
 *
 * Signals the disk driver to re-read the partition table so Windows
 * can re-mount volumes. Call after all raw I/O is complete.
 *
 * @param h Device handle (from device_open)
 */
void device_unlock(HANDLE h);

/**
 * Close a device handle.
 *
 * Releases the device handle. Safe to call with INVALID_HANDLE_VALUE
 * or NULL.
 *
 * @param h Device handle
 */
void device_close(HANDLE h);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_H */
