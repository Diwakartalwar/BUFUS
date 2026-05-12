/**
 * @file buffer_pool.h
 * @brief Memory pool for sector-aligned I/O buffers
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Opaque buffer pool type                                            */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Opaque buffer pool structure.
 *
 * Manages a fixed-size pool of VirtualAlloc'd (page-aligned) buffers
 * suitable for FILE_FLAG_NO_BUFFERING I/O operations. Thread-safe
 * via internal mutex and semaphore.
 */
typedef struct buffer_pool buffer_pool_t;

/* ───────────────────────────────────────────────────────────────── */
/* Pool lifecycle                                                     */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Create a new buffer pool.
 *
 * Allocates the pool structure and pre-allocates all buffers using
 * VirtualAlloc to ensure page alignment for direct I/O operations.
 *
 * @param buf_size Size of each buffer in bytes
 * @param count    Number of buffers to allocate (must be > 0)
 *
 * @return Pointer to new pool, NULL on allocation failure
 */
buffer_pool_t *pool_create(size_t buf_size, int count);

/**
 * Destroy a buffer pool and free all resources.
 *
 * Releases all allocated buffers and the pool structure itself.
 * Safe to call with NULL pointer.
 *
 * @param p Pool pointer
 */
void pool_destroy(buffer_pool_t *p);

/* ───────────────────────────────────────────────────────────────── */
/* Buffer acquisition and release                                     */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Acquire a buffer from the pool (blocking).
 *
 * Blocks until at least one buffer becomes available, then
 * atomically marks it as in-use and returns it.
 *
 * @param p Pool pointer
 * @return Pointer to acquired buffer (non-NULL guaranteed if pool was created successfully)
 */
void *pool_acquire(buffer_pool_t *p);

/**
 * Release a buffer back to the pool.
 *
 * Marks the buffer as free and signals waiting acquire calls.
 * Safe to call from any thread. No-op if buffer was not from this pool.
 *
 * @param p   Pool pointer
 * @param buf Buffer pointer (should be from pool_acquire)
 */
void pool_release(buffer_pool_t *p, void *buf);

/* ───────────────────────────────────────────────────────────────── */
/* Pool queries                                                       */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Get the size of each buffer in the pool.
 *
 * @param p Pool pointer
 * @return Buffer size in bytes, 0 if p is NULL
 */
size_t pool_buf_size(const buffer_pool_t *p);

#ifdef __cplusplus
}
#endif

#endif /* BUFFER_POOL_H */
