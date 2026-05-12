#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "io.h"
#include "logger.h"

static bool seek_abs(HANDLE h, uint64_t off) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    return SetFilePointerEx(h, li, NULL, FILE_BEGIN) != 0;
}

static bool get_pos(HANDLE h, uint64_t *out) {
    LARGE_INTEGER zero, cur;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(h, zero, &cur, FILE_CURRENT)) return false;
    *out = (uint64_t)cur.QuadPart;
    return true;
}

bufus_err_t io_open_source(const char *path, HANDLE *out, uint64_t *out_size) {
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

bufus_err_t io_write_image(const io_params_t *p) {
    uint32_t blk = p->block_size ? p->block_size : BUFUS_DEFAULT_BLOCK;
    BYTE *buf = (BYTE *)malloc(blk);
    if (!buf) {
        LOGE("malloc(%u) failed", blk);
        return BUFUS_ERR_NOMEM;
    }

    if (!seek_abs(p->src, 0)) {
        LOGE("SetFilePointerEx failed for source seek (error %lu)", GetLastError());
        free(buf);
        return BUFUS_ERR_IO_READ;
    }
    if (!seek_abs(p->dst, 0)) {
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
        uint64_t dst_pos_before = 0;
        uint64_t dst_pos_after  = 0;

        if (!seek_abs(p->src, written_total)) {
            LOGE("Source seek failed at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        DWORD bytes_read = 0;
        if (!ReadFile(p->src, buf, to_read, &bytes_read, NULL) || bytes_read == 0) {
            LOGE("ReadFile error at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        if (!get_pos(p->dst, &dst_pos_before)) {
            LOGE("Failed to query destination position before write (error %lu)",
                 GetLastError());
            rc = BUFUS_ERR_IO_WRITE;
            break;
        }

        if (!seek_abs(p->dst, written_total)) {
            LOGE("Destination seek failed at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_WRITE;
            break;
        }

        LOGD("WRITE chunk: target_off=%llu bytes=%lu dst_pos_before=%llu",
             written_total, (unsigned long)bytes_read, dst_pos_before);

        DWORD bytes_written = 0;
        if (!WriteFile(p->dst, buf, bytes_read, &bytes_written, NULL) ||
            bytes_written != bytes_read) {
            LOGE("WriteFile error at offset %llu (error %lu)",
                 written_total, GetLastError());
            rc = BUFUS_ERR_IO_WRITE;
            break;
        }

        if (!get_pos(p->dst, &dst_pos_after)) {
            LOGE("Failed to query destination position after write (error %lu)",
                 GetLastError());
            rc = BUFUS_ERR_IO_WRITE;
            break;
        }

        LOGD("WRITE done : wrote=%lu dst_pos_after=%llu expected=%llu",
             (unsigned long)bytes_written, dst_pos_after,
             written_total + (uint64_t)bytes_written);

        if (dst_pos_after != written_total + (uint64_t)bytes_written) {
            LOGE("Destination pointer did not advance as expected: got=%llu expected=%llu",
                 dst_pos_after, written_total + (uint64_t)bytes_written);
            rc = BUFUS_ERR_IO_WRITE;
            break;
        }

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
