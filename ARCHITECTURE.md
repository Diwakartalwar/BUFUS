# BUFUS Architecture

## Overview

BUFUS is a single-process, single-threaded native Windows application for raw USB
device imaging. The architecture is deliberately shallow — there are no abstraction
layers for the sake of abstraction. Every module maps to a concrete concern and
talks to Win32 directly.

## Module Map

```
bufus.exe
├── main.c           — CLI parsing, orchestration, privilege enforcement
├── device.c         — PhysicalDrive enumeration, open, lock, unlock
├── disk.c           — Geometry queries, sanitize, layout refresh, MBR wipe
├── io.c             — Source file read, destination write, progress callback
├── verify.c         — Post-write byte-for-byte verification
├── image_probe.c    — Image type classification (MBR/GPT/ISO9660/UEFI)
├── benchmark.c      — Sequential read/write throughput measurement
├── logger.c         — Timestamped, leveled, optionally-file-logged diagnostics
├── ui.c             — Terminal output: header, device table, progress bar, prompts
├── buffer_pool.c    — Pre-allocated page-aligned buffer pool (defined, not yet wired)
└── bufus.h          — Shared types, error codes, constants
```

## Data Flow: Write Operation

```
┌─────────────┐
│  main.c     │  parse_args() → validate → run_write()
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  io.c       │  io_open_source() → GetFileSizeEx
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  image_probe│  image_probe() → classify MBR/GPT/ISO9660/UEFI
│  .c         │  image_probe_dd_safe() → gate raw write
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  device.c   │  device_open() → CreateFileA(\\.\\PhysicalDriveN)
│             │  device_lock()  → FSCTL_LOCK_VOLUME per volume on drive
│             │  disk.c        → disk_sanitize_layout() (head + tail wipe)
│             │  disk.c        → disk_refresh_layout()
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  io.c       │  io_write_image()
│             │    loop: ReadFile(src) → seek(dst) → WriteFile(dst)
│             │    periodic progress callback → ui_progress()
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  verify.c   │  verify_image() ← only if --verify
│             │    loop: ReadFile(src) + ReadFile(dst) → memcmp
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  device.c   │  device_unlock() → IOCTL_DISK_UPDATE_PROPERTIES
│             │  device_close()
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  ui.c       │  ui_print_success() or ui_print_error()
└─────────────┘
```

## Concurrency Model

BUFUS is currently single-threaded. The buffer pool (`buffer_pool.c`) was written in
anticipation of async I/O but is not yet integrated. Future work will introduce
overlapped I/O with `CreateIoCompletionPort` for the write and verify paths.

When async I/O lands, the architecture will change to:

- A fixed pool of page-aligned buffers (currently 8 slots, `MAX_POOL_BUFS`)
- A producer thread reading from the source file
- A consumer thread writing to the device
- The pool's semaphore controls backpressure

This is not implemented yet. Do not be alarmed by the dead buffer pool code.

## Error Handling

All functions return `bufus_err_t`. The convention is:

- Return the error immediately on failure
- Clean up owned resources on the error path before returning
- Log the error at the site where it's detected (not at the caller)

The `bufus_err_str()` function converts codes to strings. There are no exceptions,
no `longjmp`, no C++ — just return codes and `goto fail` patterns.

## Memory Model

- Source file is opened with `FILE_FLAG_SEQUENTIAL_SCAN` — the OS prefetches for us
- Device is opened without buffering flags (yet) — writes go through the cache
- `VirtualAlloc` is used where page-alignment is required (benchmark, MBR wipe)
- `malloc` is used elsewhere. No custom allocator.