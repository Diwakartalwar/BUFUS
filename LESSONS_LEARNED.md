# Lessons Learned

This document captures technical lessons discovered during BUFUS
development. Some of these are things we got wrong. Some are things
we got right only after trying the wrong approach first.

---

## 1. Volume Locking Is Harder Than It Looks

**What we learned:** Windows has no single API to "lock an entire physical
drive." Instead, you must enumerate every volume on the system, query which
physical drive each volume belongs to, and lock them individually.

**Original mistake:** The first attempt locked only the first matching volume
and assumed that was sufficient. It worked on test hardware with one
partition but failed on drives with multiple volumes (e.g., a USB drive
with a data partition and a boot partition).

**Current approach:** Walk all volumes, lock and dismount every one that
belongs to the target physical drive. This is what `device_lock()` does
today in `device.c`.

**Still imperfect:** If any volume is busy (pagefile, antivirus, open
Explorer window), the lock fails. We should add retry logic with a timeout.

---

## 2. `FILE_FLAG_NO_BUFFERING` Requires More Than You Think

**What we learned:** Using `FILE_FLAG_NO_BUFFERING` on the device handle
sounds like a performance optimization. In practice, it requires:

- All read/write buffers must be aligned to the sector size (not just
  page-aligned — sector-aligned)
- All read/write offsets must be a multiple of the sector size
- All read/write sizes must be a multiple of the sector size
- File pointer must be explicitly managed (no `FILE_APPEND_DATA`)

**Original mistake:** We enabled `FILE_FLAG_NO_BUFFERING` in early
prototypes without adjusting the buffer pool alignment. Every write
failed with `ERROR_INVALID_PARAMETER`. Took hours to realize the buffer
address, not just the size, had to be sector-aligned.

**Current approach:** We use buffered I/O (the default) for simplicity.
The buffer pool was pre-allocated with `VirtualAlloc` (which guarantees
page alignment, enough for 512-byte and 4096-byte sectors) in anticipation
of switching to unbuffered I/O later.

**When to switch:** When async I/O lands, we should benchmark buffered
vs. unbuffered. For USB-attached storage, the difference may be negligible
because the bus is the bottleneck.

---

## 3. `malloc` vs `VirtualAlloc` for I/O Buffers

**What we learned:** `malloc` returns memory aligned to 8 or 16 bytes
(depending on platform). This is fine for general-purpose use but NOT for
direct disk I/O on Windows. `ReadFile`/`WriteFile` with `FILE_FLAG_NO_BUFFERING`
requires sector-aligned buffers.

**Original mistake:** Used `malloc` for benchmark buffers. Worked fine for
buffered I/O but would fail with `ERROR_INVALID_USER_BUFFER` under
`FILE_FLAG_NO_BUFFERING`.

**Current approach:** `VirtualAlloc` for any buffer that might be used
with raw device I/O. `malloc` for everything else (source file buffers,
string formatting, etc.).

---

## 4. Don't Open the Device Until You're Ready to Write

**What we learned:** Opening a physical drive with
`GENERIC_READ | GENERIC_WRITE` is a privileged operation. If the handle
is open and the user hits Ctrl+C, the handle leaks and the drive may
remain locked until the process exits.

**Original mistake:** In early prototypes, the device was opened during
argument parsing, before validating the source file or confirming with
the user. If the user canceled, the device handle leaked.

**Current approach:** `main.c` validates everything first (source file,
image probe, user confirmation), then opens the device as the last step
before the write loop. The device is closed in a `goto cleanup` pattern.

---

## 5. Image Probing Is Surprisingly Tricky

**What we learned:** Distinguishing between "hybrid ISO" (writable to USB)
and "pure ISO9660" (optical-only) is harder than expected. The naive
approach is "if it has an ISO9660 filesystem, it's an ISO." But many
bootable ISOs are hybrid — they contain both an MBR partition table AND
an ISO9660 filesystem.

**Original mistake:** The first classifier checked for ISO9660 first.
Since most bootable ISOs have ISO9660 signatures, they were classified as
"optical only" and the user got a scary error message about raw writes.
We had to reorder the checks: MBR/GPT first, then ISO9660, then UEFI
path scanning.

**Current approach:** `image_probe.c` checks in this order:
1. MBR signature at bytes 510-511
2. GPT header at offset 512
3. ISO9660 PVD at sector 16
4. UEFI boot path (linear scan up to 128 MiB)

If GPT or MBR is present, it's a disk image (hybrid or raw). Only if
those are absent and ISO9660 is present is it classified as optical-only.

---

## 6. Benchmark Results Are Lies (Sometimes)

**What we learned:** Benchmark throughput depends heavily on:
- Whether the drive's internal cache is warm or cold
- Whether the host's USB controller has write-back caching enabled
- Whether Windows' Superfetch/prefetcher is active on the source file

**Original mistake:** Reported a single benchmark run as "the" performance
number. Results varied by 30% between runs on the same hardware.

**Current approach:** The benchmark writes and reads 256 MiB of data.
This is large enough to overflow most drive caches and average out
variance, but small enough to complete quickly. Still, results should
be treated as indicative, not authoritative.

---

## 7. `printf` During I/O Slows Things Down

**What we learned:** Calling `printf` (or any console output) inside the
inner write loop measurably reduces throughput, especially on fast
hardware where I/O is the bottleneck.

**Original mistake:** The progress callback was called for every block
and formatted a full status line each time. On a 4 MiB block size with
a 4 GB image, that's 1000 `printf` calls.

**Current approach:** The progress bar uses `\r` to overwrite the same
line, reducing console output. For future async I/O, we should consider
updating the UI at most 10-20 times per second, not once per block.

---

## 8. `uint64_t` Math Has Pitfalls

**What we learned:** Mixing `uint64_t` and `DWORD` (which is `uint32_t`)
in arithmetic or comparisons causes implicit widening that can produce
correct-but-subtle results. More dangerously, casting `uint64_t` to
`DWORD` (as `ReadFile`/`WriteFile` require) truncates silently.

**Original mistake:** `DWORD bytes_read = (DWORD)remaining;` — when
`remaining` is a `uint64_t` larger than 4 GB, this truncates silently.
The current code has a guard (`remaining < blk`) that prevents this,
but it was added during review, not during initial implementation.

**Current approach:** All casts to `DWORD` are guarded by range checks.
Block sizes are capped at `UINT32_MAX`.

---

## 9. Windows Error Codes Are Not Portable

**What we learned:** Error codes from `GetLastError()` are specific to the
Windows version and sometimes the device driver. Two different USB
controllers return different error codes for the same failure.

**Original mistake:** We mapped specific error codes to BUFUS error codes
early on. This broke when a different USB controller returned a different
code for "device not ready."

**Current approach:** `device_open()` checks for `ERROR_ACCESS_DENIED`
specifically (to distinguish privilege errors from other failures) but
otherwise maps most Win32 errors to generic `BUFUS_ERR_OPEN` or
`BUFUS_ERR_WIN32`. The specific error is logged but not relied upon
for control flow.

---

## 10. Build System Matters More Than Features at This Stage

**What we learned:** Without a Makefile, the project can't be built
reliably by anyone other than the original developer. Every new
contributor asks "how do I build this?" and has to reverse-engineer
the compilation command from the CI config or ask in an issue.

**Original mistake:** We focused on features (verify, benchmark, image
probe) before getting the build system right. Now there are 10 source
files, 10 headers, and no way to compile them with a single command.

**Current approach:** Build system is the highest-priority item in the
ROADMAP.md. A Makefile with `make`, `make release`, and `make clean`
targets will unblock all other development.