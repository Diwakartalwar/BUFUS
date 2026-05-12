/**
 * @file benchmark.h
 * @brief Benchmarking utilities for disk I/O performance measurement
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include <windows.h>
#include "bufus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Benchmark results structure                                       */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Benchmark result metrics.
 */
typedef struct {
    double   write_mbs;        /**< Write speed in MiB/s             */
    double   read_mbs;         /**< Read speed in MiB/s              */
    uint64_t bytes_tested;     /**< Total bytes tested               */
} bench_result_t;

/* ───────────────────────────────────────────────────────────────── */
/* Benchmarking operations                                            */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Run sequential read/write benchmark on a device.
 *
 * Performs sequential write followed by sequential read to measure
 * throughput. Allocates an aligned buffer and fills it with a known
 * pattern before testing.
 *
 * @param dev        Device handle (opened with FILE_FLAG_NO_BUFFERING)
 * @param block_size Block size for I/O operations (bytes)
 * @param sector_size Device sector size (bytes), 0 for default 512
 * @param test_bytes Total bytes to test
 * @param out        Pointer to bench_result_t for results (non-NULL)
 *
 * @return BUFUS_OK on success, error code on failure
 */
bufus_err_t benchmark_run(HANDLE      dev,
                          uint32_t    block_size,
                          uint32_t    sector_size,
                          uint64_t    test_bytes,
                          bench_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BENCHMARK_H */
