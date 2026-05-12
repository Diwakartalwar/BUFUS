#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "io.h"
#include "logger.h"

/* ── Source file open ─────────────────────────────────────────────── */

bufus_err_t io_open_source(const char *path, HANDLE *out, uint64_t *out_size) {
    /*
     * Regular buffered open — the source ISO is a normal file
     * and does not need NO_BUFFERING alignment constraints.
     * FILE_FLAG_SEQUENTIAL_SCAN hints the prefetcher.
     */
    HANDLE h = CreateFileA(path,
                           GENERIC_READ,
                           FILE_SHARE_READ,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN,
                           NULL);

    if (h == INVALID_HANDLE_VALUE) {
        LOGE("Cannot open source '%s' (error %lu)", path, GetLastError());
        return BUFUS_ERR_OPEN;
    }

    LARGE_INTEGER sz;
    sz.QuadPart = 0;
    if (!GetFileSizeEx(h, &sz)) {
        LOGE("GetFileSizeEx failed for '%s'", path);
        CloseHandle(h);
        return BUFUS_ERR_IO_READ;
    }

    *out      = h;
    *out_size = (uint64_t)sz.QuadPart;
    LOGI("Source '%s': %.2f MiB (%llu bytes)",
         path,
         (double)*out_size / (1024.0 * 1024.0),
         *out_size);

    return BUFUS_OK;
}

/* ── Write pipeline ───────────────────────────────────────────────── */

bufus_err_t io_write_image(const io_params_t *p) {
    /*
     * The destination device was opened with FILE_FLAG_NO_BUFFERING, so:
     *   • WriteFile buffer address must be VirtualAlloc'd (page-aligned).
     *   • Transfer size must be a non-zero multiple of sector_size.
     *
     * Strategy: read up to `blk` bytes from the source (any size),
     * zero-pad the tail to the next sector boundary, then write
     * the padded block to the device.  This is safe because the
     * ISO image will be smaller than the drive.
     */
    uint32_t sect = (p->sector_size > 0) ? p->sector_size : 512;
    uint32_t blk  = ((p->block_size + sect - 1) / sect) * sect;
    if (blk == 0) blk = sect;

    BYTE *buf = (BYTE *)VirtualAlloc(NULL, blk,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!buf) {
        LOGE("VirtualAlloc(%u) failed", blk);
        return BUFUS_ERR_NOMEM;
    }

    /* Seek both handles to the start */
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(p->src, zero, NULL, FILE_BEGIN)) {
        LOGE("SetFilePointerEx failed for source seek (error %lu)", GetLastError());
        VirtualFree(buf, 0, MEM_RELEASE);
        return BUFUS_ERR_IO_READ;
    }
    if (!SetFilePointerEx(p->dst, zero, NULL, FILE_BEGIN)) {
        LOGE("SetFilePointerEx failed for destination seek (error %lu)", GetLastError());
        VirtualFree(buf, 0, MEM_RELEASE);
        return BUFUS_ERR_IO_WRITE;
    }

    uint64_t written_total = 0;

    LARGE_INTEGER t_start, t_now, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);

    bufus_err_t rc = BUFUS_OK;

    while (written_total < p->src_size) {
        uint64_t remaining = p->src_size - written_total;
        DWORD to_read = (DWORD)(remaining < (uint64_t)blk ? remaining : blk);

        /* Sector-aligned write length (≥ to_read) */
        DWORD aligned = ((to_read + sect - 1) / sect) * sect;
        if (aligned > blk) aligned = blk;

        /* Zero the tail pad so we never write stale data */
        memset(buf, 0, aligned);

        DWORD bytes_read = 0;
        if (!ReadFile(p->src, buf, to_read, &bytes_read, NULL)
            || bytes_read == 0) {
            LOGE("ReadFile error at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        DWORD bytes_written = 0;
        if (!WriteFile(p->dst, buf, aligned, &bytes_written, NULL)
            || bytes_written != aligned) {
            LOGE("WriteFile error at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_WRITE;
            break;
        }

        /* Advance by the bytes we actually consumed from the source */
        written_total += bytes_read;

        if (p->progress) {
            QueryPerformanceCounter(&t_now);
            double elapsed = (double)(t_now.QuadPart - t_start.QuadPart)
                           / (double)freq.QuadPart;
            double speed = (elapsed > 0.0)
                         ? ((double)written_total / (1024.0 * 1024.0)) / elapsed
                         : 0.0;
            p->progress(written_total, p->src_size, speed, p->userdata);
        }
    }

    FlushFileBuffers(p->dst);
    VirtualFree(buf, 0, MEM_RELEASE);

    if (rc == BUFUS_OK) {
        LOGI("Write complete: %llu bytes written to device", written_total);
    }
    return rc;
}
