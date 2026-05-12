#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <string.h>
#include "device.h"
#include "logger.h"

/* ── Internal: query properties for one PhysicalDriveN ───────────── */

static bufus_err_t query_drive(int idx, device_info_t *info) {
    char path[DEVICE_PATH_LEN];
    snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", idx);

    /* Open with zero access for a read-only query */
    HANDLE h = CreateFileA(path,
                           0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return BUFUS_ERR_NOT_FOUND;

    memset(info, 0, sizeof(*info));
    info->index       = idx;
    info->sector_size = 512; /* safe default */
    strncpy(info->path, path, DEVICE_PATH_LEN - 1);

    DWORD ret = 0;

    /* ── Geometry & size ── */
    DISK_GEOMETRY_EX geo;
    memset(&geo, 0, sizeof(geo));
    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                        NULL, 0, &geo, sizeof(geo), &ret, NULL)) {
        info->size_bytes  = (uint64_t)geo.DiskSize.QuadPart;
        info->sector_size = geo.Geometry.BytesPerSector;
        if (info->sector_size == 0) info->sector_size = 512;
    }

    /* ── Model string & bus type ── */
    char prop_buf[4096];
    memset(prop_buf, 0, sizeof(prop_buf));

    STORAGE_PROPERTY_QUERY spq;
    memset(&spq, 0, sizeof(spq));
    spq.PropertyId = StorageDeviceProperty;
    spq.QueryType  = PropertyStandardQuery;

    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                        &spq, sizeof(spq),
                        prop_buf, sizeof(prop_buf),
                        &ret, NULL)) {
        STORAGE_DEVICE_DESCRIPTOR *desc =
            (STORAGE_DEVICE_DESCRIPTOR *)prop_buf;

        /* Copy model string (may be NULL offset) */
        if (desc->ProductIdOffset != 0 &&
            desc->ProductIdOffset < ret) {
            strncpy(info->model,
                    prop_buf + desc->ProductIdOffset,
                    DEVICE_MODEL_LEN - 1);
            /* trim trailing spaces */
            int l = (int)strlen(info->model);
            while (l > 0 && info->model[l - 1] == ' ') info->model[--l] = '\0';
        }

        /* Also try vendor string if model is empty */
        if (info->model[0] == '\0' &&
            desc->VendorIdOffset != 0 &&
            desc->VendorIdOffset < ret) {
            strncpy(info->model,
                    prop_buf + desc->VendorIdOffset,
                    DEVICE_MODEL_LEN - 1);
            int l = (int)strlen(info->model);
            while (l > 0 && info->model[l - 1] == ' ') info->model[--l] = '\0';
        }

        info->is_removable = (desc->BusType == BusTypeUsb) ||
                             (desc->RemovableMedia != 0);
    }

    CloseHandle(h);
    return BUFUS_OK;
}

/* ── Public API ───────────────────────────────────────────────────── */

bufus_err_t device_enumerate(device_list_t *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < BUFUS_MAX_DRIVES; i++) {
        device_info_t info;
        if (query_drive(i, &info) == BUFUS_OK) {
            out->drives[out->count++] = info;
        }
    }
    LOGI("Found %d physical drive(s)", out->count);
    return BUFUS_OK;
}

bufus_err_t device_get_info(int index, device_info_t *out) {
    return query_drive(index, out);
}

bufus_err_t device_open(int index, HANDLE *out_handle) {
    char path[DEVICE_PATH_LEN];
    snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", index);

    /* Correctness-first mode: use buffered device I/O while validating behavior. */
    HANDLE h = CreateFileA(path,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);

    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        LOGE("Cannot open %s (Win32 error %lu)", path, err);
        return (err == ERROR_ACCESS_DENIED) ? BUFUS_ERR_PRIVILEGE : BUFUS_ERR_OPEN;
    }

    LOGI("Opened %s for exclusive access", path);
    *out_handle = h;
    return BUFUS_OK;
}

bufus_err_t device_lock(HANDLE h, int index) {
    (void)h;
    /*
     * Walk every volume letter / GUID path.  For each volume that
     * lives on our physical drive, dismount it so Windows releases
     * file system locks before we stomp on the raw sectors.
     */
    char vol[MAX_PATH];
    HANDLE hf = FindFirstVolumeA(vol, MAX_PATH);
    if (hf == INVALID_HANDLE_VALUE) {
        LOGE("FindFirstVolume failed (%lu)", GetLastError());
        return BUFUS_ERR_LOCK;
    }

    do {
        /* Strip trailing backslash — CreateFile can't open "X:\\" */
        size_t len = strlen(vol);
        if (len > 0 && vol[len - 1] == '\\') vol[len - 1] = '\0';

        HANDLE hv = CreateFileA(vol,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (hv == INVALID_HANDLE_VALUE) {
            vol[len - 1] = '\\';
            continue;
        }

        /* Check physical drive number for this volume */
        STORAGE_DEVICE_NUMBER sdn;
        memset(&sdn, 0, sizeof(sdn));
        DWORD rb = 0;
        if (DeviceIoControl(hv, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                            NULL, 0, &sdn, sizeof(sdn), &rb, NULL)) {
            if ((int)sdn.DeviceNumber == index) {
                DWORD dummy = 0;
                /* Lock prevents other processes writing to the volume */
                if (!DeviceIoControl(hv, FSCTL_LOCK_VOLUME,
                                     NULL, 0, NULL, 0, &dummy, NULL)) {
                    LOGE("FSCTL_LOCK_VOLUME failed for PhysicalDrive%d (error %lu)",
                         index, GetLastError());
                    CloseHandle(hv);
                    FindVolumeClose(hf);
                    return BUFUS_ERR_LOCK;
                }
                /* Dismount flushes and removes the mounted filesystem */
                if (!DeviceIoControl(hv, FSCTL_DISMOUNT_VOLUME,
                                     NULL, 0, NULL, 0, &dummy, NULL)) {
                    LOGE("FSCTL_DISMOUNT_VOLUME failed for PhysicalDrive%d (error %lu)",
                         index, GetLastError());
                    CloseHandle(hv);
                    FindVolumeClose(hf);
                    return BUFUS_ERR_LOCK;
                }
                LOGI("Locked and dismounted volume on PhysicalDrive%d", index);
            }
        }

        vol[len - 1] = '\\'; /* restore for FindNextVolume */
        CloseHandle(hv);
    } while (FindNextVolumeA(hf, vol, MAX_PATH));

    FindVolumeClose(hf);
    return BUFUS_OK;
}

void device_unlock(HANDLE h) {
    /* Ask the disk driver to re-read the partition table */
    DWORD dummy = 0;
    DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES,
                    NULL, 0, NULL, 0, &dummy, NULL);
    LOGI("Device unlocked, partition table refresh requested");
}

void device_close(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        LOGD("Device handle closed");
    }
}
