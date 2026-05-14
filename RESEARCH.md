# BUFUS — Research Notes

This document tracks technical research, decisions, and references relevant
to BUFUS development. Not everything here made it into the code — some paths
were explored and abandoned.

---

## 1. FILE_FLAG_NO_BUFFERING vs Buffered I/O

### Problem
Windows offers two modes for file/device I/O:
- **Buffered** (default): OS cache manager mediates all reads/writes.
  Alignment is flexible. Good for general-purpose throughput.
- **Unbuffered** (`FILE_FLAG_NO_BUFFERING`): Bypasses cache manager.
  Requires sector-aligned buffers and sector-aligned offsets/sizes.
  Mandatory for raw disk writes — otherwise `WriteFile` returns
  `ERROR_INVALID_PARAMETER`.

### Current State
`device_open()` does NOT use `FILE_FLAG_NO_BUFFERING`. Writes go through
the cache manager. This works but means:
- The OS may reorder writes (fine for sequential imaging, risky for
  crash consistency)
- We pay cache manager overhead on both source reads and device writes
- We cannot guarantee write ordering during a crash

### Decision
Keep buffered I/O for now. The performance delta on USB-attached storage is
minimal because the USB bus is the bottleneck, not the cache path.
Switching to unbuffered I/O requires the buffer pool with page-aligned
memory, sector-aligned block sizes, and careful offset arithmetic.

When async I/O is implemented, we should revisit this. Unbuffered +
overlapped is the path to maximum throughput on NVMe/USB 3.x.

**Reference:** Microsoft Docs — "File Buffering" section of CreateFileA

---

## 2. Volume Locking Strategy

### Problem
Before writing to a raw physical drive, we must ensure no other process has
a filesystem handle open on any volume hosted by that drive. If we don't,
`WriteFile` to the physical device silently succeeds, but the filesystem
driver has stale cached data and will overwrite our writes when it flushes.

### Approach
`device_lock()` in `device.c` enumerates all mount points via
`FindFirstVolumeA`/`FindNextVolumeA`, opens each, queries its physical drive
number via `IOCTL_STORAGE_GET_DEVICE_NUMBER`, and issues `FSCTL_LOCK_VOLUME`
+ `FSCTL_DISMOUNT_VOLUME` for matching volumes.

### Known Issues
- `device_lock()` currently ignores the `HANDLE h` parameter passed to it.
  It walks all volumes every time. This is inefficient but functionally
  correct for now.
- If a volume is busy (e.g., pagefile, antivirus scanning), the lock will
  fail. We return `BUFUS_ERR_LOCK` immediately. A more robust approach
  would be to retry with a timeout.
- We don't lock volumes we can't open (e.g., if another process has exclusive
  access). This is a gap — those volumes won't be dismounted.

### Alternatives Considered
- **IOCTL_STORAGE_EJECTION_CONTROL**: Prevents media removal but doesn't
  dismount filesystems. Not sufficient alone.
- **IOCTL_DISK_UPDATE_PROPERTIES on DeviceObject**: Only refreshes
  partition table, doesn't lock anything.
- **ZwFsControlFile with FSCTL_DISMOUNT_VOLUME**: Same as
  `DeviceIoControl`, just a different API path.

**Decision:** Current approach is acceptable for v0.1. Extend with retry
logic and handle locked volumes more gracefully.

---

## 3. Disk Sanitization (Why We Zero Head + Tail)

### Problem
When writing a smaller image to a larger USB drive, leftover partition
table entries and filesystem structures from the previous contents may
persist. This can cause:
- BIOS/UEFI to attempt booting from stale MBR/GPT entries
- OS mounting old partitions that still exist in the partition table
- Confusion about the drive's actual contents

### Approach
`disk_sanitize_layout()` zeros:
- First 16 MiB: Covers MBR (512 bytes), all primary GPT headers/entries
  (typically within first 1 MiB), and any hybrid MBR+GPT structures
- Last 16 MiB: Covers GPT backup header and backup partition table

These sizes are generous. A GPT header is 1 sector (512 bytes), partition
entries are typically 128 entries × 128 bytes = 64 KiB. The backup header
mirrors the primary. 16 MiB is overkill for any realistic layout but costs
less than 1 second on modern hardware.

### What This Does NOT Cover
- Does NOT zero the full disk (intentional — would take hours on large
  drives and wear out flash)
- Does NOT perform cryptographic erasure (irrelevant for USB flash)
- Does NOT handle hidden sectors or host-protected areas (HPA/DCO on
  SATA drives — not applicable to USB)

### Future Work
- Wipe MBR specifically before writing a hybrid ISO (currently the
  `disk_wipe_mbr()` function exists but is never called)
- Consider partial sanitization for performance: only wipe regions that
  will NOT be overwritten by the image

---

## 4. Image Probing Heuristics

### Problem
We need to determine whether a file is:
1. A raw disk image (dd-style, directly writable to USB)
2. A hybrid ISO (contains both ISO9660 filesystem and MBR/GPT partition
   table — can be written raw to USB and will boot)
3. A pure ISO9660 optical image (has El Torito but no partition table —
   cannot be written raw; needs filesystem-level handling)

### Current Heuristics

| Check | Offset | Method |
|-------|--------|--------|
| MBR signature | bytes 510-511 | `0x55AA` |
| Protective MBR (GPT) | byte 450 | type `0xEE` |
| GPT header | bytes 512+ | magic `"EFI PART"` |
| ISO9660 PVD | sector 16, offset 1 | magic `"CD001"` |
| UEFI boot path | variable | scan for `EFI\BOOT\BOOTX64.EFI` string |

### Decision Logic
```
has_gpt OR has_mbr → could be disk image
  + has_iso9660 → hybrid ISO (IMG_KIND_HYBRID_ISO)
  - no iso9660 → raw disk image (IMG_KIND_DISK_IMAGE)
has_iso9660 only → optical only (IMG_KIND_ISO9660_ONLY)
neither → unknown (IMG_KIND_UNKNOWN)
```

### Limitations
- Does not validate MBR partition table entries (just checks signature)
- Does not parse GPT entries — just checks for header presence
- ISO9660 PVD check is at a fixed offset (sector 16). Some non-standard
  ISOs may place the PVD elsewhere (e.g., with a boot catalog offset)
- UEFI path scan covers 128 MiB. Very large ISOs with deeply nested
  directories might have the path beyond this range, though in practice
  `BOOTX64.EFI` is always near the root

### Abandoned Approaches
- **libmagic / file signature database**: Overkill dependency. Our heuristics
  cover 99.9% of real-world images.
- **Full GPT parsing**: Added complexity for no practical benefit — we only
  need to know IF a GPT exists, not parse its entries.
- **El Torito parsing**: Would let us extract the boot catalog and offer
  "bootable ISO" detection, but doesn't change our write behavior.

---

## 5. Benchmark Methodology

### Design Decisions
- Uses `VirtualAlloc` for the test buffer (page-aligned, avoids malloc
  overhead affecting results)
- Fills buffer with a deterministic pattern (`i ^ 0xA5 ^ (i >> 8)`) to
  prevent the OS from optimizing away zero-page writes
- Writes first, then reads back — tests both directions separately
- Uses `QueryPerformanceCounter` for timing (microsecond resolution)
- Test size is 256 MiB — large enough to average out variance, small
  enough to run in ~5 seconds on slow USB 2.0

### Limitations
- Only measures sequential throughput. Does not test random I/O.
- Does not account for USB controller caching (some controllers have
  write-back cache that inflates write numbers)
- Does not measure sustained throughput over time (no thermal throttling
  detection for USB flash drives)
- Single-threaded — does not saturate USB 3.x bandwidth in most cases

---

## 6. Why Not Use `dd` or `Win32 Disk Imager`

`dd` on Windows (via Cygwin/MSYS2):
- Requires POSIX compatibility layer
- Does not handle Windows drive locking properly
- No volume dismount before write
- No image type detection
- No verification built in

Win32 Disk Imager:
- Open source but unmaintained (last release 2015-era)
- No benchmark mode
- No image probing (no hybrid ISO detection)
- No command-line interface

BUFUS fills the gap: native Win32, small binary, CLI-first with plans for
GUI, proper volume management, and active maintenance.

---

## 7. Binary Size Constraints

### Target: < 1 MB (GUI), < 300 KB (CLI)

Current strategy:
- Pure C, no runtime dependencies
- Static linking with MinGW (`-static`)
- Strip debug symbols (`-s`)
- Optimize for size (`-Os`)
- No CRT dynamic linking (use `-nostdlib` with careful Win32 entry)

### Measured (estimated from source size)
- Source files: ~70 KB total
- With MinGW static linking: ~200-400 KB expected
- GUI resources (future): +50-100 KB
- Well within target

### Things That Would Bloat the Binary
- OpenSSL/libmbed TLS (for hash verification — avoid, use native Win32
  `BCrypt` API instead)
- Dynamic linking to MSVCRT (adds DLL dependency)
- XML/JSON config parsers (not needed — CLI args only)

**Reference:** Stripped MinGW "hello world" is ~30 KB. Our code is ~10x
that complexity. Budget: ~300 KB for CLI, ~1 MB with GUI resources.