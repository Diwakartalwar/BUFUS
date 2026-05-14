# BUFUS Roadmap

## Current State (v0.1.0-alpha)

Working:
- [x] Physical drive enumeration (`device_enumerate`)
- [x] Drive open/lock/unlock/close cycle
- [x] Source file open and size query
- [x] Raw sequential image write with progress
- [x] Post-write byte-for-byte verification
- [x] Image type probing (MBR/GPT/ISO9660/UEFI)
- [x] Disk sanitization (head/tail wipe before write)
- [x] Benchmark mode (sequential write + read throughput)
- [x] Colored terminal UI with progress bar
- [x] File logging with levels

Not working / not implemented:
- [ ] Async/overlapped I/O
- [ ] Buffer pool integration into write path
- [ ] Hash-based verification (SHA-256)
- [ ] Write error retry logic
- [ ] Post-write speed/EIA summary
- [ ] Build system (Makefile is empty)
- [ ] Windows resource file (version info)
- [ ] Partition/offset write targeting

---

## Phase 1 — Stabilize Core (Windows Only)

**Goal:** Make the existing synchronous path rock-solid.

### 1.1 — Async I/O Integration
- Wire `buffer_pool` into `io_write_image()`
- Implement producer/consumer with `CreateIoCompletionPort`
- Benchmark against current sync path — expect 20-40% improvement on NVMe,
  marginal gain on USB 2.0 (bus-bound either way)
- Keep sync path as fallback for debugging

### 1.2 — Error Recovery
- Add retry logic for transient write failures (up to 3 retries per block)
- Add source file validation before acquiring device lock
- Validate block size is a multiple of sector size

### 1.3 — Build System
- Write Makefile for MinGW-w64
- Add `-Os -s -static -mconsole` flags
- Target binary size: < 300 KB for CLI
- Add clean/release/debug targets
- Consider CMake as optional second path

### 1.4 — Verification Improvement
- Add `--hash` mode: compute SHA-256 of source file, then compute SHA-256
  of device bytes post-write, compare hashes
- Keep `--verify` (byte-for-byte) as default for paranoid users
- Hash mode will be ~100x faster on large images since we skip the device read

### 1.5 — Usability
- Print summary after write: total bytes, elapsed time, average write speed
- Add ETA to progress bar
- Add `--skip-verify` explicit flag
- Add `--yes` flag to skip all confirmations (for scripting)

---

## Phase 2 — Linux Support

### 2.1 — POSIX Compatibility Layer
- Abstract `CreateFileA` → `open()` with `O_DIRECT`
- Abstract `DeviceIoControl` → `ioctl()` calls
- Abstract `FILE_SHARE_*` semantics (Linux doesn't need drive locking the same way)
- Abstract `VirtualAlloc` → `posix_memalign`

### 2.2 — Linux-Specific Concerns
- `/dev/sdX` path handling (enumeration via `/sys/block/`)
- `blkdiscard` for TRIM/discard support
- `SG_IO` for SCSI command passthrough if needed
- udev rules for non-root operation (e.g., `TAG+="uaccess"`)
- Handle `O_EXCL` on block devices (kernel already enforces single-writer)

### 2.3 — Testing
- CI on GitHub Actions with QEMU disk images
- FUSE-based mock devices for safe testing

---

## Phase 3 — macOS Support

### 3.1 — macOS/Darwin Port
- `/dev/rdiskN` path handling (raw disk nodes are faster)
- `iokit` framework for device enumeration and ejection
- `fcntl(F_NOCACHE)` replaces `FILE_FLAG_NO_BUFFERING`
- `MAP_NOCACHE` for buffer alignment

---

## Phase 4 — GUI (Stretch Goal)

- Native Win32 dialog, no frameworks
- Drag-and-drop ISO onto window
- Drive selection dropdown with icons
- Estimated time display
- Cancel operation mid-write (drain + cleanup)

Target: still < 1 MB binary. This means no Qt, no GTK, no WPF, no webview.
Pure Win32 API with hand-drawn controls if necessary.

---

## Phase 5 — Advanced Features (Post-1.0)

- Partition-aware imaging (write ISO to a specific partition)
- Multi-image USB (partition the USB, write multiple images)
- Write to `.img` file (disk-to-file clone)
- Compression support (zstd for image streaming)
- Bad sector handling / remapping
- Syslinux/GRUB bootloader installation helper
- UEFI Secure Boot key enrollment helper

---

## Version Targets

| Version    | Target                                |
|------------|---------------------------------------|
| 0.1.x      | CLI stabilized, async I/O, hash verify |
| 0.2.x      | Linux support                         |
| 0.3.x      | macOS support                         |
| 1.0.0      | Stable API, tested edge cases         |
| 1.1.x      | GUI alpha                             |
| 2.0.0      | GUI complete, multi-image support     |