#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "verify.h"
#include "logger.h"

bufus_err_t verify_image(const verify_params_t *p) {
    uint32_t blk = p->block_size ? p->block_size : BUFUS_DEFAULT_BLOCK;
    BYTE *src_buf = (BYTE *)malloc(blk);
    BYTE *dst_buf = (BYTE *)malloc(blk);
    if (!src_buf || !dst_buf) {
        free(src_buf);
        free(dst_buf);
        LOGE("malloc failed for verify buffers");
        return BUFUS_ERR_NOMEM;
    }

    /* Rewind both handles */
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(p->src, zero, NULL, FILE_BEGIN)) {
        LOGE("Verify: SetFilePointerEx failed for source seek (error %lu)", GetLastError());
        free(src_buf);
        free(dst_buf);
        return BUFUS_ERR_IO_READ;
    }
    if (!SetFilePointerEx(p->dst, zero, NULL, FILE_BEGIN)) {
        LOGE("Verify: SetFilePointerEx failed for destination seek (error %lu)", GetLastError());
        free(src_buf);
        free(dst_buf);
        return BUFUS_ERR_IO_READ;
    }

    LARGE_INTEGER t_start, t_now, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);

    uint64_t    verified = 0;
    bufus_err_t rc       = BUFUS_OK;

    while (verified < p->size) {
        uint64_t remaining = p->size - verified;
        DWORD to_read = (DWORD)(remaining < (uint64_t)blk ? remaining : blk);
        memset(src_buf, 0, to_read);
        memset(dst_buf, 0, to_read);

        /* Read from original source (buffered — any size OK) */
        DWORD r_src = 0;
        if (!ReadFile(p->src, src_buf, to_read, &r_src, NULL) || r_src == 0) {
            LOGE("Verify: ReadFile on source failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        /* Read from device using same byte size to compare exact payload. */
        DWORD r_dst = 0;
        if (!ReadFile(p->dst, dst_buf, to_read, &r_dst, NULL) || r_dst == 0) {
            LOGE("Verify: ReadFile on device failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }
        if (r_dst != r_src) {
            LOGE("Verify: short read on device at offset %llu (%lu vs %lu)",
                 verified, (unsigned long)r_dst, (unsigned long)r_src);
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        /* Compare only the source bytes (ignore sector-pad on device) */
        if (memcmp(src_buf, dst_buf, r_src) != 0) {
            LOGE("Verify: DATA MISMATCH at offset %llu – %llu",
                 verified, verified + r_src);
            rc = BUFUS_ERR_VERIFY;
            break;
        }

        verified += r_src;

        if (p->progress) {
            QueryPerformanceCounter(&t_now);
            double elapsed = (double)(t_now.QuadPart - t_start.QuadPart)
                           / (double)freq.QuadPart;
            double speed = (elapsed > 0.0)
                         ? ((double)verified / (1024.0 * 1024.0)) / elapsed
                         : 0.0;
            p->progress(verified, p->size, speed, p->userdata);
        }
    }

    free(src_buf);
    free(dst_buf);

    if (rc == BUFUS_OK) {
        LOGI("Verify OK: %llu bytes match", verified);
    }
    return rc;
}
