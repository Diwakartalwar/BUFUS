#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "verify.h"
#include "logger.h"

bufus_err_t verify_image(const verify_params_t *p) {
    uint32_t sect = (p->sector_size > 0) ? p->sector_size : 512;
    uint32_t blk  = ((p->block_size + sect - 1) / sect) * sect;
    if (blk == 0) blk = sect;

    /*
     * Two separate VirtualAlloc buffers:
     *   src_buf — receives data from the source file (buffered read, any size).
     *   dst_buf — receives data from the device (NO_BUFFERING, must be
     *             page-aligned and size must be a sector multiple).
     */
    BYTE *src_buf = (BYTE *)VirtualAlloc(NULL, blk,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    BYTE *dst_buf = (BYTE *)VirtualAlloc(NULL, blk,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    if (!src_buf || !dst_buf) {
        if (src_buf) VirtualFree(src_buf, 0, MEM_RELEASE);
        if (dst_buf) VirtualFree(dst_buf, 0, MEM_RELEASE);
        LOGE("VirtualAlloc failed for verify buffers");
        return BUFUS_ERR_NOMEM;
    }

    /* Rewind both handles */
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    SetFilePointerEx(p->src, zero, NULL, FILE_BEGIN);
    SetFilePointerEx(p->dst, zero, NULL, FILE_BEGIN);

    LARGE_INTEGER t_start, t_now, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);

    uint64_t    verified = 0;
    bufus_err_t rc       = BUFUS_OK;

    while (verified < p->size) {
        uint64_t remaining = p->size - verified;
        DWORD to_read = (DWORD)(remaining < (uint64_t)blk ? remaining : blk);
        DWORD aligned = ((to_read + sect - 1) / sect) * sect;
        if (aligned > blk) aligned = blk;

        memset(src_buf, 0, aligned);
        memset(dst_buf, 0, aligned);

        /* Read from original source (buffered — any size OK) */
        DWORD r_src = 0;
        if (!ReadFile(p->src, src_buf, to_read, &r_src, NULL) || r_src == 0) {
            LOGE("Verify: ReadFile on source failed at offset %llu (error %lu)",
                 verified, GetLastError());
            rc = BUFUS_ERR_IO_READ;
            break;
        }

        /* Read from device (NO_BUFFERING — must be sector-aligned size) */
        DWORD r_dst = 0;
        if (!ReadFile(p->dst, dst_buf, aligned, &r_dst, NULL) || r_dst == 0) {
            LOGE("Verify: ReadFile on device failed at offset %llu (error %lu)",
                 verified, GetLastError());
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

    VirtualFree(src_buf, 0, MEM_RELEASE);
    VirtualFree(dst_buf, 0, MEM_RELEASE);

    if (rc == BUFUS_OK) {
        LOGI("Verify OK: %llu bytes match", verified);
    }
    return rc;
}