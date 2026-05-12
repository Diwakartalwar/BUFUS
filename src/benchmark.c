#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "benchmark.h"
#include "logger.h"

bufus_err_t benchmark_run(HANDLE      dev,
                          uint32_t    block_size,
                          uint32_t    sector_size,
                          uint64_t    test_bytes,
                          bench_result_t *out) {
    if (!out) return BUFUS_ERR_ARGS;

    uint32_t sect = (sector_size > 0) ? sector_size : 512;
    uint32_t blk  = ((block_size + sect - 1) / sect) * sect;
    if (blk == 0) blk = sect;

    BYTE *buf = (BYTE *)VirtualAlloc(NULL, blk,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!buf) {
        LOGE("benchmark_run: VirtualAlloc(%u) failed", blk);
        return BUFUS_ERR_NOMEM;
    }
    for (uint32_t i = 0; i < blk; i++) buf[i] = (BYTE)(i ^ 0xA5u ^ (i >> 8));

    LARGE_INTEGER zero, t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    zero.QuadPart = 0;

    if (!SetFilePointerEx(dev, zero, NULL, FILE_BEGIN)) {
        LOGE("benchmark_run: seek to start failed before write (error %lu)",
             GetLastError());
        VirtualFree(buf, 0, MEM_RELEASE);
        return BUFUS_ERR_IO_WRITE;
    }

    uint64_t written = 0;
    QueryPerformanceCounter(&t0);

    while (written < test_bytes) {
        DWORD w = 0;
        if (!WriteFile(dev, buf, blk, &w, NULL) || w == 0 || w != blk) {
            LOGE("benchmark write failed at %llu bytes (error %lu)",
                 written, GetLastError());
            VirtualFree(buf, 0, MEM_RELEASE);
            return BUFUS_ERR_IO_WRITE;
        }
        written += w;
    }

    FlushFileBuffers(dev);
    QueryPerformanceCounter(&t1);

    double write_sec = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    out->write_mbs   = (write_sec > 0.0)
                     ? ((double)written / (1024.0 * 1024.0)) / write_sec
                     : 0.0;

    if (!SetFilePointerEx(dev, zero, NULL, FILE_BEGIN)) {
        LOGE("benchmark_run: seek to start failed before read (error %lu)",
             GetLastError());
        VirtualFree(buf, 0, MEM_RELEASE);
        return BUFUS_ERR_IO_READ;
    }

    uint64_t read_total = 0;
    QueryPerformanceCounter(&t0);

    while (read_total < written) {
        DWORD r = 0;
        if (!ReadFile(dev, buf, blk, &r, NULL) || r == 0 || r != blk) {
            LOGE("benchmark read failed at %llu bytes (error %lu)",
                 read_total, GetLastError());
            VirtualFree(buf, 0, MEM_RELEASE);
            return BUFUS_ERR_IO_READ;
        }
        read_total += r;
    }

    QueryPerformanceCounter(&t1);

    double read_sec  = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    out->read_mbs    = (read_sec > 0.0)
                     ? ((double)read_total / (1024.0 * 1024.0)) / read_sec
                     : 0.0;
    out->bytes_tested = written;

    VirtualFree(buf, 0, MEM_RELEASE);

    LOGI("Benchmark - Write: %.1f MiB/s  Read: %.1f MiB/s  (%.0f MiB tested)",
         out->write_mbs, out->read_mbs,
         (double)written / (1024.0 * 1024.0));

    return BUFUS_OK;
}