#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "verify.h"
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

    if (!seek_abs(p->src, 0)) {
        LOGE("Verify: source seek to start failed (error %lu)", GetLastError());
        free(src_buf);
        free(dst_buf);
        return BUFUS_ERR_IO_READ;
    }
    if (!seek_abs(p->dst, 0)) {
        LOGE("Verify: destination seek to start failed (error %lu)", GetLastError());
        free(src_buf);
        free(dst_buf);
        return BUFUS_ERR_IO_READ;
    }

    LARGE_INTEGER t_start, t_now, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);

    uint64_t verified = 0;
    bufus_err_t rc = BUFUS_OK;

    while (verified < p->size) {
        uint64_t remaining = p->size - verified;
        DWORD to_read = (DWORD)(remaining < (uint64_t)blk ? remaining : blk);
        uint64_t dst_pos_before = 0;
        uint64_t dst_pos_after = 0;

        if (!seek_abs(p->src, verified)) {
            LOGE("Verify: source seek failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }
        if (!seek_abs(p->dst, verified)) {
            LOGE("Verify: destination seek failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }
        if (!get_pos(p->dst, &dst_pos_before)) {
            LOGE("Verify: failed to query destination position before read (error %lu)",
                 GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        LOGD("VERIFY chunk: target_off=%llu bytes=%lu dst_pos_before=%llu",
             verified, (unsigned long)to_read, dst_pos_before);

        DWORD r_src = 0;
        if (!ReadFile(p->src, src_buf, to_read, &r_src, NULL) || r_src == 0) {
            LOGE("Verify: ReadFile on source failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        DWORD r_dst = 0;
        if (!ReadFile(p->dst, dst_buf, to_read, &r_dst, NULL) || r_dst == 0) {
            LOGE("Verify: ReadFile on device failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        if (!get_pos(p->dst, &dst_pos_after)) {
            LOGE("Verify: failed to query destination position after read (error %lu)",
                 GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        LOGD("VERIFY done : read=%lu dst_pos_after=%llu expected=%llu",
             (unsigned long)r_dst, dst_pos_after,
             verified + (uint64_t)r_dst);

        if (r_dst != r_src) {
            LOGE("Verify: short read on device at offset %llu (%lu vs %lu)",
                 verified, (unsigned long)r_dst, (unsigned long)r_src);
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        if (memcmp(src_buf, dst_buf, r_src) != 0) {
            LOGE("Verify: DATA MISMATCH at offset %llu - %llu",
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
