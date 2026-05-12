#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <string.h>
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
    SetFilePointerEx(h, origin, NULL, FILE_BEGIN);

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