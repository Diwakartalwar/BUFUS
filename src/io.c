#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
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
    uint32_t blk = p->block_size ? p->block_size : BUFUS_DEFAULT_BLOCK;
    BYTE *buf = (BYTE *)malloc(blk);
    if (!buf) {
        LOGE("malloc(%u) failed", blk);
        return BUFUS_ERR_NOMEM;
    }

    /* Seek both handles to the start */
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(p->src, zero, NULL, FILE_BEGIN)) {
        LOGE("SetFilePointerEx failed for source seek (error %lu)", GetLastError());
        free(buf);
        return BUFUS_ERR_IO_READ;
    }
    if (!SetFilePointerEx(p->dst, zero, NULL, FILE_BEGIN)) {
        LOGE("SetFilePointerEx failed for destination seek (error %lu)", GetLastError());
        free(buf);
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

        DWORD bytes_read = 0;
        if (!ReadFile(p->src, buf, to_read, &bytes_read, NULL)
            || bytes_read == 0) {
            LOGE("ReadFile error at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        DWORD bytes_written = 0;
        if (!WriteFile(p->dst, buf, bytes_read, &bytes_written, NULL)
            || bytes_written != bytes_read) {
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

    if (!FlushFileBuffers(p->dst)) {
        LOGE("FlushFileBuffers failed (error %lu)", GetLastError());
        rc = BUFUS_ERR_IO_WRITE;
    }
    free(buf);

    if (rc == BUFUS_OK) {
        LOGI("Write complete: %llu bytes written to device", written_total);
    }
    return rc;
}
