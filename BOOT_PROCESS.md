# Boot Process: What Happens When You Run BUFUS

This document traces the exact sequence of operations from `main()` to
completed write, with Win32 API calls and internal data state at each step.

---

## Startup Sequence

```
main()
├── SetConsoleOutputCP(CP_UTF8)
│   → Enables UTF-8 block character rendering (█ ░ ██ etc.)
│
├── ui_print_header()
│   → Prints ASCII art logo + version string
│   → Calls SetConsoleTextAttribute for colored output
│
├── parse_args(argc, argv) → bufus_cfg_t
│   → memset(&cfg, 0, sizeof(cfg))
│   → cfg.drive_index = -1
│   → cfg.block_size  = BUFUS_DEFAULT_BLOCK (4 MiB)
│   → Loops through argv, fills cfg fields
│   → Returns fully populated cfg
│
├── log_init(cfg.log_file, cfg.verbose)
│   → Creates mutex via CreateMutexA
│   → Opens log file via fopen_s (if --log specified)
│   → Sets g_log.min_level (LOG_DEBUG if --verbose, LOG_INFO otherwise)
│
└── bufus_is_elevated()
    → OpenProcessToken(PROCESS_QUERY_INFORMATION)
    → GetTokenInformation(TokenElevation)
    → Returns elev.TokenIsElevated != 0
    → If false: prints error, log_close(), return 1
```

### State After Startup
- Console output is UTF-8
- Logging is active (file and/or console)
- We know we're running elevated
- cfg is populated with user's choices

---

## Drive Enumeration

```
device_enumerate(&devlist)
├── memset(&devlist, 0, sizeof(devlist))
│
├── for i = 0..BUFUS_MAX_DRIVES (32)
│   └── query_drive(i, &info)
│       ├── snprintf(path, "\\\\.\\PhysicalDrive%d", i)
│       ├── CreateFileA(path, 0, FILE_SHARE_READ|WRITE, ...)
│       │   → Opens with zero access (query only)
│       │   → If INVALID → return BUFUS_ERR_NOT_FOUND (drive doesn't exist)
│       │
│       ├── info.index = i
│       ├── info.sector_size = 512 (safe default)
│       │
│       ├── IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
│       │   → Fills DISK_GEOMETRY_EX
│       │   → info.size_bytes = geo.DiskSize.QuadPart
│       │   → info.sector_size = geo.Geometry.BytesPerSector
│       │
│       ├── IOCTL_STORAGE_QUERY_PROPERTY
│       │   → StorageDeviceProperty, PropertyStandardQuery
│       │   → Parses STORAGE_DEVICE_DESCRIPTOR
│       │   → Extracts model string (ProductIdOffset)
│       │   → Extracts vendor fallback (VendorIdOffset)
│       │   → info.is_removable = (BusType == BusTypeUsb) || RemovableMedia
│       │
│       └── CloseHandle(h)
│       → returns BUFUS_OK
│   └── devlist.drives[devlist.count++] = info
│
└── LOGI("Found %d physical drive(s)", devlist.count)
```

### State After Enumeration
- `devlist.count` = number of physical drives
- Each `devlist.drives[i]` has: index, size, sector_size, model, is_removable
- Handles are all closed (query-only, no locks held)

---

## --list Path (Optional Exit)

```
if (cfg.drive_index == DRIVE_LIST_SENTINEL)
├── ui_list_devices(&devlist)
│   ├── Print column headers: IDX / MODEL / SIZE / TYPE
│   ├── For each drive:
│   │   ├── gb = d->size_bytes / (1024^3)
│   │   ├── type = d->is_removable ? "USB/Removable" : "Fixed"
│   │   ├── set_color(d->is_removable ? COL_GREEN : COL_RESET)
│   │   └── printf("[%d]  %-34s  %7.2f GB  %s\n", ...)
│   └── set_color(COL_RESET)
├── log_close()
└── return 0
```

---

## Validation Phase

```
// Drive index check
if (cfg.drive_index < 0) → error: "No target drive specified"

// Get target drive info
device_get_info(cfg.drive_index, &dinfo)
├── returns query_drive(cfg.drive_index, &dinfo)
├── If BUFUS_ERR_NOT_FOUND → error: "Drive not found"
│
└── dinfo now contains: size_bytes, sector_size, model, is_removable

// Removable check
if (!dinfo.is_removable && !cfg.force)
└── error: "Refusing to target a fixed/non-removable disk without --force"

// Source check (if not --benchmark mode)
if (cfg.source[0] == '\0')
└── error: "No source image specified"
```

### State After Validation
- We know the exact target drive and its properties
- We know the user has confirmed which drive to use (by index)
- We have the source file path (if writing, not benchmarking)

---

## Write Flow (run_write)

### Phase 1: Source Analysis

```
io_open_source(cfg.source, &src, &src_size)
├── CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, ..., FILE_FLAG_SEQUENTIAL_SCAN)
│   → Buffered I/O, sequential access hint for OS prefetching
│
├── GetFileSizeEx(h, &sz)
│   → sz.QuadPart = file size in bytes
│
└── Returns: src handle, src_size

printf("  Source : %s\n", cfg.source)
printf("          %.2f MiB\n", src_size / 1024²)
printf("  Target : PhysicalDrive%d - %s\n", cfg.drive_index, dinfo.model)
printf("          %.2f GB\n", dinfo.size_bytes / 1024³)
```

```
// Size check
if (src_size > dinfo.size_bytes)
└── error: "Source image is larger than the target drive"

// Image probing
image_probe(src, src_size, &probe)
├── Seek to offset 0, read 512 bytes → check MBR signature (0x55AA at bytes 510-511)
├── If src_size >= 1024: read sector 1 → check GPT magic ("EFI PART")
├── If src_size >= 16*2048+6: read sector 16 → check ISO9660 PVD ("CD001")
├── Scan up to 128 MiB for "EFI/BOOT/BOOTX64.EFI" (case-insensitive)
├── Classifies as: IMG_KIND_DISK_IMAGE, IMG_KIND_HYBRID_ISO,
│   IMG_KIND_ISO9660_ONLY, or IMG_KIND_UNKNOWN
│
└── Returns: image_probe_t with flags and kind

printf("  Image  : %s\n", image_kind_str(probe.kind))
printf("          MBR=%s  GPT=%s  ISO9660=%s  UEFIPathHint=%s\n", ...)
```

```
// Safety gate
if (!image_probe_dd_safe(&probe) && !cfg.force)
└── error: "This image does not look like a raw USB-disk image..."
    "  Refusing raw write to avoid creating a non-bootable USB."
    "  Re-run with --force only if you explicitly want raw ISO write."
```

### Phase 2: User Confirmation

```
if (!cfg.force)
├── snprintf(prompt, "ALL data on PhysicalDrive%d will be permanently erased. Continue?", ...)
├── ui_confirm(prompt)
│   ├── set_color(COL_YELLOW)
│   ├── printf("  %s [y/N]: ", prompt)
│   ├── fflush(stdout)
│   ├── Read characters via getchar() until '\n' or EOF
│   ├── return (first_char == 'y' || first_char == 'Y')
│
└── If !confirmed → printf("\n  Aborted.\n\n"), close src, return 0
```

### Phase 3: Device Acquisition

```
device_open(cfg.drive_index, &dev)
├── snprintf(path, "\\\\.\\PhysicalDrive%d", index)
├── CreateFileA(path, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|WRITE,
│              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
│   → Note: NOT using FILE_FLAG_NO_BUFFERING yet
├── If INVALID_HANDLE_VALUE:
│   ├── GetLastError()
│   ├── ERROR_ACCESS_DENIED → BUFUS_ERR_PRIVILEGE
│   └── else → BUFUS_ERR_OPEN
│
└── Returns: device handle

device_lock(dev, cfg.drive_index)
├── FindFirstVolumeA(vol, MAX_PATH)
│   → Starts volume enumeration
│
├── do {
│   ├── CreateFileA(vol, GENERIC_READ|WRITE, FILE_SHARE_READ|WRITE, ...)
│   ├── IOCTL_STORAGE_GET_DEVICE_NUMBER → STORAGE_DEVICE_NUMBER
│   ├── if (sdn.DeviceNumber == index) {
│   │   ├── FSCTL_LOCK_VOLUME → blocks other processes
│   │   ├── FSCTL_DISMOUNT_VOLUME → unmounts filesystem
│   │   └── LOGI("Locked and dismounted volume...")
│   │   }
│   ├── FindNextVolumeA(...)
│   } while (more volumes)
│
└── FindVolumeClose(hf)
```

### Phase 4: Disk Sanitization

```
disk_sanitize_layout(dev, dinfo.size_bytes, src_size)
├── WIPE_MB = 16 MiB
├── head = min(disk_size, WIPE_MB)
├── tail = min(disk_size, WIPE_MB)
├──
├── wipe_range(h, 0, head)
│   ├── VirtualAlloc(NULL, 1 MiB, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)
│   │   → Calloc'd (zero-filled) 1 MiB buffer
│   ├── SetFilePointerEx(h, {0}, NULL, FILE_BEGIN)
│   ├── Loop: Write 1 MiB chunks until 'head' bytes written
│   │   ├── WriteFile(h, zeros, n, &w, NULL)
│   │   └── Check w == n
│   └── Free(zeros)
│
├── if (disk_size > tail):
│   └── wipe_range(h, disk_size - tail, tail)
│       → Same as above, zeros last 16 MiB
│
└── FlushFileBuffers(h)
```

```
disk_refresh_layout(dev)
├── IOCTL_DISK_UPDATE_PROPERTIES → force re-read partition table
├── IOCTL_DISK_GET_DRIVE_LAYOUT_EX → log new layout (best-effort)
└── Return BUFUS_OK
```

### Phase 5: Write Loop

```
io_params_t wp = {
    .src         = src,           // source file handle
    .dst         = dev,           // device handle
    .src_size    = src_size,      // total bytes to write
    .block_size  = cfg.block_size, // default 4 MiB
    .sector_size = sector,        // dinfo.sector_size or 512
    .progress    = ui_progress,   // callback for progress bar
    .userdata    = NULL,
}

io_write_image(&wp)
├── uint32_t blk = wp.block_size ? wp.block_size : BUFUS_DEFAULT_BLOCK
├── BYTE *buf = malloc(blk)  // single I/O buffer
│
├── seek_abs(src, 0)  // rewind source to beginning
├── seek_abs(dst, 0)  // rewind device to beginning
│
├── LARGE_INTEGER t_start; QueryPerformanceCounter(&t_start)
│
├── uint64_t written_total = 0
│
├── while (written_total < wp->src_size) {
│   │
│   ├── remaining = src_size - written_total
│   ├── to_read = min(remaining, blk)    // DWORD cast, guarded
│   │
│   ├── // Read from source
│   ├── seek_abs(src, written_total)
│   ├── ReadFile(src, buf, to_read, &bytes_read, NULL)
│   │   → On failure: rc = BUFUS_ERR_IO_READ, break
│   │
│   ├── // Write to device
│   ├── seek_abs(dst, written_total)
│   ├── WriteFile(dst, buf, bytes_read, &bytes_written, NULL)
│   │   → On failure or short write: rc = BUFUS_ERR_IO_WRITE, break
│   │
│   ├── // Validate position
│   ├── get_pos(dst, &dst_pos_after)
│   │   → Sanity check: dst_pos_after == written_total + bytes_written
│   │   → Mismatch: rc = BUFUS_ERR_IO_WRITE, break
│   │
│   ├── written_total += bytes_read
│   │
│   └── // Progress callback (every block)
│       QueryPerformanceCounter(&t_now)
│       elapsed  = (t_now - t_start) / freq
│       speed_mbs = written_total / (1024²) / elapsed
│       ui_progress(written_total, src_size, speed_mbs, NULL)
│         ├── pct = done/total * 100
│         ├── filled = pct / 100 * 42 (bar width)
│         ├── printf("\r  [██░░░░░░░░░░░░░░░░░░░░]  %5.1f%%  %6.1f MiB/s  %.0f/%.0f MiB",
│         ├── fflush(stdout)
│         └── if done >= total: printf("\n")
│ }
│
├── FlushFileBuffers(dst)  // ensure all data hits physical media
│
└── Return rc (BUFUS_OK on success)
```

### Phase 6: Verify (Optional)

```
if (cfg.verify) {
│
│   // Reopen device (TODO: optimize to reuse handle)
│   device_open(cfg.drive_index, &dev)
│   device_lock(dev, cfg.drive_index)
│
│   verify_params_t vp = {
│       .src         = src,
│       .dst         = dev,
│       .size        = src_size,
│       .block_size  = cfg.block_size,
│       .sector_size = sector,
│       .progress    = ui_progress,
│       .userdata    = NULL,
│   };
│
│   verify_image(&vp)
│   ├── malloc src_buf (blk bytes), dst_buf (blk bytes)
│   ├── seek_abs(src, 0), seek_abs(dst, 0)
│   │
│   ├── while (verified < size) {
│   │   ├── ReadFile(src, src_buf, to_read, &r_src)  // from file
│   │   ├── ReadFile(dst, dst_buf, to_read, &r_dst)  // from device
│   │   ├── if r_dst != r_src → BUFUS_ERR_IO_READ
│   │   ├── if memcmp(src_buf, dst_buf, r_src) != 0 → BUFUS_ERR_VERIFY
│   │   ├── verified += r_src
│   │   └── progress callback
│   │ }
│   │
│   ├── free(src_buf), free(dst_buf)
│   └── Return rc
│
│   device_unlock(dev)
│   device_close(dev)
│
│   if (rc != BUFUS_OK) → error: "Verification failed — data mismatch"
}
```

---

## Device Release & Cleanup

```
// Final layout refresh (regardless of verify)
device_open(cfg.drive_index, &dev)   // reopen for refresh
if (dev != INVALID_HANDLE_VALUE) {
│   disk_refresh_layout(dev)          // partition table re-read
│   device_close(dev)
}

CloseHandle(src)     // close source file
ui_print_success()   // ✓ Done! Drive is ready.
log_close()          // flush log file, close mutex
return 0
```

---

## Error Path Cleanup

At any failure point, the cleanup order is:

```
1. Unlock device    (device_unlock)  — only if locked
2. Close device     (device_close)  — only if opened
3. Close source     (CloseHandle)   — only if opened
4. Print error      (ui_print_error)
5. Close log        (log_close)
6. Return non-zero
```

Currently, the error-handling code in `main.c` does not follow this
order consistently. Some paths skip `device_unlock` if `device_lock`
failed. Some paths attempt to close a handle that was never opened.
This is tracked as a known issue.

---

## Performance Timeline (4 GB ISO, USB 3.0 SSD)

```
Operation                        Duration     % of Total
─────────────────────────────────────────────────────
Startup + parse args              ~5 ms        <1%
Drive enumeration                 ~100 ms       1%
Source file open                  ~2 ms        <1%
Image probing                     ~5 ms        <1%
User confirmation                 variable      —
Device open + lock                ~100 ms       1%
Sanitization (head + tail)        ~400 ms       3%
Layout refresh                    ~50 ms        0.5%
Write (4 GB, ~49 MB/s)          ~82 seconds    92%
Verification (if enabled)        ~90 seconds    —
Layout refresh (post-write)       ~50 ms        0.1%
Cleanup                            ~5 ms        <1%
─────────────────────────────────────────────────────
Total (write only)               ~84 seconds
Total (write + verify)          ~174 seconds
```

---

## Sequence Diagram

```
User                  main.c                  device.c                disk.c
  │                      │                       │                      │
  │  bufus.exe -s X.iso  │                       │                      │
  │  -d 2 --verify       │                       │                      │
  │─────────────────────>│                       │                      │
  │                      │  parse_args()         │                      │
  │                      │──────────────────────>│                      │
  │                      │  bufus_cfg_t          │                      │
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │                      │  device_enumerate()   │                      │
  │                      │──────────────────────>│  query_drive(0..31)  │
  │                      │                       │─────────────────────>│
  │                      │                       │  CreateFileA()       │
  │                      │                       │  IOCTL_*             │
  │                      │                       │<─────────────────────│
  │                      │<──────────────────────│                      │
  │  ┌───────────────────│──────────────────────>│                      │
  │  │ device_info[]     │                       │                      │
  │  └───────────────────│<──────────────────────│                      │
  │                      │                       │                      │
  │  [user confirms]     │                       │                      │
  │                      │  device_open(2)       │                      │
  │                      │──────────────────────>│  CreateFileA()       │
  │                      │                       │  (GENERIC_READ|WRITE)│
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │                      │  device_lock(h, 2)    │                      │
  │                      │──────────────────────>│  FindFirstVolumeA()  │
  │                      │                       │  FSCTL_LOCK_VOLUME   │
  │                      │                       │  FSCTL_DISMOUNT      │
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │                      │  disk_sanitize()      │                      │
  │                      │──────────────────────>│  WriteFile(zeros)    │
  │                      │                       │  FlushFileBuffers()  │
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │  [========== WRITE LOOP ==========]          │                      │
  │                      │                       │                      │
  │                      │  io_write_image()     │                      │
  │                      │──────────────────────>│                      │
  │                      │   ReadFile(src)       │                      │
  │                      │   seek(dst)           │                      │
  │                      │   WriteFile(dst)      │                      │
  │                      │   (repeat × ~1000)    │                      │
  │                      │   FlushFileBuffers()  │                      │
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │  [if --verify]       │                       │                      │
  │                      │  verify_image()       │                      │
  │                      │──┬───────────────────>│                      │
  │                      │  │ ReadFile(src)      │                      │
  │                      │  │ ReadFile(dst)      │                      │
  │                      │  │ memcmp()           │                      │
  │                      │<─┘────────────────────│                      │
  │                      │                       │                      │
  │                      │  device_unlock(h)     │                      │
  │                      │──────────────────────>│  IOCTL_*_PROPERTIES  │
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │                      │  device_close(h)      │                      │
  │                      │──────────────────────>│  CloseHandle()       │
  │                      │<──────────────────────│                      │
  │                      │                       │                      │
  │  ✓ Done!             │                       │                      │
  │<─────────────────────│                       │                      │
```