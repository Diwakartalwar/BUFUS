#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <assert.h>
#include "buffer_pool.h"

#define MAX_POOL_BUFS 8

struct buffer_pool {
    size_t  buf_size;
    int     count;
    void   *bufs[MAX_POOL_BUFS];
    bool    in_use[MAX_POOL_BUFS];
    HANDLE  semaphore;   /* counts available buffers */
    HANDLE  mutex;       /* guards in_use[] array    */
};

/* ── Lifecycle ────────────────────────────────────────────────────── */

buffer_pool_t *pool_create(size_t buf_size, int count) {
    assert(count > 0 && count <= MAX_POOL_BUFS);
    assert(buf_size > 0);

    buffer_pool_t *p = (buffer_pool_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->buf_size  = buf_size;
    p->count     = count;
    p->semaphore = CreateSemaphoreA(NULL, (LONG)count, (LONG)count, NULL);
    p->mutex     = CreateMutexA(NULL, FALSE, NULL);

    if (!p->semaphore || !p->mutex) goto fail;

    for (int i = 0; i < count; i++) {
        /* VirtualAlloc guarantees page-aligned (≥ 4096 B) memory —
           more than enough for any sector size up to 4096 B.       */
        p->bufs[i] = VirtualAlloc(NULL, buf_size,
                                  MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
        if (!p->bufs[i]) {
            /* partial cleanup */
            for (int j = 0; j < i; j++) VirtualFree(p->bufs[j], 0, MEM_RELEASE);
            goto fail;
        }
    }
    return p;

fail:
    if (p->semaphore) CloseHandle(p->semaphore);
    if (p->mutex)     CloseHandle(p->mutex);
    free(p);
    return NULL;
}

void pool_destroy(buffer_pool_t *p) {
    if (!p) return;
    for (int i = 0; i < p->count; i++) {
        if (p->bufs[i]) VirtualFree(p->bufs[i], 0, MEM_RELEASE);
    }
    CloseHandle(p->semaphore);
    CloseHandle(p->mutex);
    free(p);
}

/* ── Acquire / Release ────────────────────────────────────────────── */

void *pool_acquire(buffer_pool_t *p) {
    /* Block until at least one buffer is free */
    WaitForSingleObject(p->semaphore, INFINITE);

    WaitForSingleObject(p->mutex, INFINITE);
    void *buf = NULL;
    for (int i = 0; i < p->count; i++) {
        if (!p->in_use[i]) {
            p->in_use[i] = true;
            buf = p->bufs[i];
            break;
        }
    }
    ReleaseMutex(p->mutex);
    return buf; /* non-NULL guaranteed when semaphore was taken */
}

void pool_release(buffer_pool_t *p, void *buf) {
    WaitForSingleObject(p->mutex, INFINITE);
    for (int i = 0; i < p->count; i++) {
        if (p->bufs[i] == buf) {
            p->in_use[i] = false;
            ReleaseMutex(p->mutex);
            ReleaseSemaphore(p->semaphore, 1, NULL);
            return;
        }
    }
    ReleaseMutex(p->mutex);
    /* buf not from this pool — no-op (caller bug) */
}

size_t pool_buf_size(const buffer_pool_t *p) {
    return p ? p->buf_size : 0;
}