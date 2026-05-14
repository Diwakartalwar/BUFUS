# Known Issues

## Bugs

### 1. `device_lock()` Ignores Its Handle Parameter
**Severity:** Medium
**File:** `device.c:130`
**Status:** Open

The `device_lock()` function accepts a `HANDLE h` parameter but never reads
from it. Instead, it enumerates ALL system volumes and locks any that map to
the given physical drive index. This works but is inefficient and could lock
volumes on the wrong drive if the index changes between enumeration and lock.

**Impact:** Functionally correct in all tested scenarios. The handle is
reserved for future use in per-volume locking.

**Fix:** Pass the device path to `device_lock()` and use it to match volumes
more precisely, or remove the unused parameter.

---

### 2. `disk_sanitize_layout()` Ignores `image_size` Parameter
**Severity:** Low
**File:** `disk.c:107`
**Status:** Open

The `(void)image_size;` cast explicitly marks the parameter as unused. The
sanitization always wipes 16 MiB from the head and 16 MiB from the tail,
regardless of how large the incoming image is.

**Impact:** Wiping more than necessary. When writing a 500 MB image to a 64 GB
USB drive, we zero 32 MiB of data that will never be read back. Wasteful but
harmless.

**Fix:** Compare `image_size` against `disk_size` and only sanitize regions
that won't be overwritten by the image. For example:
```
head_wipe = min(16 MiB, image_size)
tail_wipe = min(16 MiB, disk_size - image_size)
```

---

### 3. Memory Leak in `io_write_image()` Error Path
**Severity:** Low (one-time 4 MiB leak on write failure)
**File:** `io.c:56,164`
**Status:** Open

If any error occurs after the buffer is allocated on line 56, the `free(buf)`
on line 163 is never reached. The loop breaks and returns the error code
without freeing the buffer.

**Impact:** A single 4 MiB leak on write failure. Not cumulative — the
application exits shortly after. No practical impact but triggers Valgrind
(doesn't apply on Windows) and static analysis warnings.

**Fix:**
```c
bufus_err_t rc = BUFUS_OK;
BYTE *buf = malloc(blk);
if (!buf) return BUFUS_ERR_NOMEM;

// ... operations that may set rc to error ...

free(buf);  // Always free, regardless of rc
return rc;
```

---

### 4. Verify Reopens Device Instead of Re-Seeking
**Severity:** Low
**File:** `main.c:367-371`, `main.c:329-365`
**Status:** Open

After write completes, the device handle is closed. If `--verify` is
requested, the device is reopened, re-locked, verified, unlocked, and
closed again. The whole lock/unlock cycle repeats.

**Impact:** Adds ~100-500 ms of overhead (volume enumeration + locking).
Also fails if a volume was re-mounted between write and verify.

**Fix:** Keep the device handle open after write. Rewind both source and
destination to offset 0 and run verify without releasing the lock.

---

### 5. `strncpy` Without Guaranteed Null Termination
**Severity:** Medium
**File:** `main.c:107,135`
**Status:** Open

`strncpy(cfg.source, argv[++i], BUFUS_MAX_PATH_LEN - 1)` does not guarantee
null termination when the source string is >= 259 characters.

**Impact:** If a path is exactly 259+ characters, `cfg.source` won't be
null-terminated. Subsequent `printf`, `log_write`, or `io_open_source`
calls reading this string will read past the buffer.

**Fix:**
```c
strncpy(cfg.source, argv[++i], BUFUS_MAX_PATH_LEN - 1);
cfg.source[BUFUS_MAX_PATH_LEN - 1] = '\0';
```

Apply the same fix to `cfg.log_file`.

---

## Missing Features

### 6. No Makefile
**Severity:** High (blocks development)
**Status:** Open

The repository contains an empty `makefile` (0 bytes). All development
requires manually invoking GCC with the correct flags.

**Impact:** New contributors can't build the project. CI is impossible.
No reproducible build process.

**Fix:** Create a Makefile supporting at minimum: `make` (debug),
`make release` (optimized), `make clean`.

---

### 7. No Write Error Retry Logic
**Severity:** Medium
**Status:** Open

If `WriteFile` fails on a single block (e.g., USB glitch, transient
controller error), the entire write operation aborts immediately.

**Impact:** A momentary USB disconnect during a 10 GB write means starting
over from scratch.

**Fix:** Add retry loop per block (3 attempts with exponential backoff):
```c
for (int attempt = 0; attempt < 3; attempt++) {
    if (WriteFile(...)) break;
    Sleep(100 * (1 << attempt));  // 100ms, 200ms, 400ms
}
```

---

### 8. No Hash-Based Verification
**Severity:** Medium
**Status:** Open

`--verify` performs byte-for-byte comparison by re-reading the entire
device. For large drives, this doubles the total operation time.

**Impact:** A 4 GB write + 4 GB verify takes twice as long as it could.

**Fix:** Implement `--hash` mode that computes SHA-256 of the source file
before writing, then computes SHA-256 of the device after writing using
`BCryptHash` (native Win32, no OpenSSL). Comparing two 32-byte hashes is
instant.

---

### 9. No Post-Write Summary
**Severity:** Low
**Status:** Open

After a successful write, the program prints a "Done!" message with no
statistics.

**Impact:** User has no idea how long the operation took, what throughput
was achieved, or how many bytes were written.

**Fix:** After `io_write_image()` returns `BUFUS_OK`, print:
```
  ✓ Done! 4,294,967,296 bytes written in 87.3 seconds (49.4 MiB/s average)
```

---

### 10. No Block Size Validation Against Sector Size
**Severity:** Low
**Status:** Open

The `--block` flag accepts any value from 1 to 1024 MiB. There is no check
that the block size is a multiple of the device's sector size.

**Impact:** If a user passes `--block 3` on a 512-byte sector device, the
write will fail or produce corrupted output (misaligned writes may silently
succeed with buffered I/O but fail with `FILE_FLAG_NO_BUFFERING` in the
future).

**Fix:**
```c
if (cfg.block_size % sector != 0) {
    fprintf(stderr, "Block size %u is not a multiple of sector size %u\n",
            cfg.block_size, sector);
    return 1;
}
```

---

## Design Limitations (Accepted)

These are known architectural trade-offs, not bugs. They are tracked here
for awareness only.

### A1. Single-Threaded I/O
All I/O is sequential: read block, write block, update progress. The buffer
pool was designed for async I/O but is unused. Maximum throughput is limited
by round-trip latency per block.

**Accepted because:** USB 2.0 is bus-bound anyway. USB 3.x benefits from
async but the complexity isn't justified until the sync path is proven
reliable.

### A2. No Partial Write Recovery
If the application crashes mid-write, no resume/partial-write logic exists.
The user must re-run the entire operation.

**Accepted because:** Adding journaling or partial-write tracking adds
significant complexity. The sanitization step ensures partial writes don't
leave a bootable-but-corrupt device.

### A3. 128 MiB UEFI Scan Limit
`image_probe()` only scans the first 128 MiB for `EFI/BOOT/BOOTX64.EFI`.
Very large ISOs with deeply nested UEFI paths could be misclassified.

**Accepted because:** In practice, all standard UEFI-bootable ISOs place
the boot loader near the beginning. 128 MiB covers all known cases.

---

## Unconfirmed / Cannot Reproduce

No issues are currently listed in this category. If you encounter something
strange, file an issue with:
- Exact command line used
- Windows version (`winver`)
- Drive model (`wmic diskdrive get model`)
- Log file contents (`--log bufus.log --verbose`)