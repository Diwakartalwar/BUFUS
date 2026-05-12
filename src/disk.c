#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <string.h>
#include <stdlib.h>
#include "disk.h"
#include "logger.h"

bufus_err_t disk_get_geometry(HANDLE h, disk_geometry_t *out) {
    DISK_GEOMETRY_EX geo;
    memset(&geo, 0, sizeof(geo));
    DWORD ret = 0;

    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                         NULL, 0, &geo, sizeof(geo), &ret, NULL)) {
        LOGE("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed (error %lu)",
             GetLastError());
        return BUFUS_ERR_WIN32;
    }

    out->size_bytes    = (uint64_t)geo.DiskSize.QuadPart;
    out->sector_size   = geo.Geometry.BytesPerSector;
    if (out->sector_size == 0) out->sector_size = 512;
    out->total_sectors = out->size_bytes / out->sector_size;

    LOGI("Disk geometry: %.2f GiB, sector=%u B, sectors=%llu",
         (double)out->size_bytes / (1024.0 * 1024.0 * 1024.0),
         out->sector_size,
         out->total_sectors);

    return BUFUS_OK;
}

bufus_err_t disk_wipe_mbr(HANDLE h, uint32_t sector_size) {
    if (sector_size == 0) sector_size = 512;

    /*
     * FILE_FLAG_NO_BUFFERING requires VirtualAlloc — not malloc/calloc.
     * VirtualAlloc returns zeroed pages on Windows (unlike malloc),
     * so no explicit memset is needed.
     */
    BYTE *zeros = (BYTE *)VirtualAlloc(NULL, sector_size,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);
    if (!zeros) {
        LOGE("VirtualAlloc failed for MBR wipe buffer");
        return BUFUS_ERR_NOMEM;
    }

    LARGE_INTEGER origin;
    origin.QuadPart = 0;
    if (!SetFilePointerEx(h, origin, NULL, FILE_BEGIN)) {
        LOGE("MBR wipe seek failed (error %lu)", GetLastError());
        VirtualFree(zeros, 0, MEM_RELEASE);
        return BUFUS_ERR_IO_WRITE;
    }

    DWORD written = 0;
    BOOL  ok      = WriteFile(h, zeros, sector_size, &written, NULL);
    VirtualFree(zeros, 0, MEM_RELEASE);

    if (!ok || written != sector_size) {
        LOGE("MBR wipe WriteFile failed (error %lu)", GetLastError());
        return BUFUS_ERR_IO_WRITE;
    }

    LOGI("MBR wiped: %u bytes zeroed at sector 0", sector_size);
    return BUFUS_OK;
}

static bufus_err_t wipe_range(HANDLE h, uint64_t start, uint64_t bytes) {
    const uint32_t chunk = 1024 * 1024;
    BYTE *zeros = (BYTE *)calloc(1, chunk);
    if (!zeros) return BUFUS_ERR_NOMEM;

    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)start;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        LOGE("wipe_range seek failed at %llu (error %lu)", start, GetLastError());
        free(zeros);
        return BUFUS_ERR_IO_WRITE;
    }

    uint64_t done = 0;
    while (done < bytes) {
        DWORD n = (DWORD)((bytes - done) < chunk ? (bytes - done) : chunk);
        DWORD w = 0;
        if (!WriteFile(h, zeros, n, &w, NULL) || w != n) {
            LOGE("wipe_range write failed at %llu (error %lu)",
                 start + done, GetLastError());
            free(zeros);
            return BUFUS_ERR_IO_WRITE;
        }
        done += w;
    }

    free(zeros);
    return BUFUS_OK;
}

bufus_err_t disk_sanitize_layout(HANDLE h, uint64_t disk_size, uint64_t image_size) {
    const uint64_t WIPE_MB = 16ull * 1024 * 1024;
    uint64_t head = (disk_size < WIPE_MB) ? disk_size : WIPE_MB;
    uint64_t tail = (disk_size < WIPE_MB) ? disk_size : WIPE_MB;
    uint64_t tail_start;

    (void)image_size;

    LOGI("Sanitize: wiping first %.2f MiB and last %.2f MiB",
         (double)head / (1024.0 * 1024.0),
         (double)tail / (1024.0 * 1024.0));

    if (head > 0) {
        bufus_err_t rc = wipe_range(h, 0, head);
        if (rc != BUFUS_OK) return rc;
    }

    if (tail > 0 && disk_size > tail) {
        tail_start = disk_size - tail;
        bufus_err_t rc = wipe_range(h, tail_start, tail);
        if (rc != BUFUS_OK) return rc;
    }

    if (!FlushFileBuffers(h)) {
        LOGE("Sanitize flush failed (error %lu)", GetLastError());
        return BUFUS_ERR_IO_WRITE;
    }

    return BUFUS_OK;
}

bufus_err_t disk_refresh_layout(HANDLE h) {
    DWORD dummy = 0;
    DRIVE_LAYOUT_INFORMATION_EX layout;
    DWORD ret = 0;

    if (!DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES,
                         NULL, 0, NULL, 0, &dummy, NULL)) {
        LOGE("IOCTL_DISK_UPDATE_PROPERTIES failed (error %lu)", GetLastError());
        return BUFUS_ERR_WIN32;
    }

    memset(&layout, 0, sizeof(layout));
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                         NULL, 0, &layout, sizeof(layout), &ret, NULL)) {
        LOGW("IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed (error %lu) after refresh",
             GetLastError());
    }

    LOGI("Disk layout refresh requested");
    return BUFUS_OK;
}
