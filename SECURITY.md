# BUFUS Security Considerations

## Threat Model

BUFUS runs with Administrator privileges and performs raw disk I/O. This means
any bug in the write path can cause arbitrary data corruption on any physical
drive visible to the system. The threat model is:

1. **User error** — specifying the wrong drive index
2. **Software bugs** — writing to wrong offsets, corrupting partition tables
3. **Malicious inputs** — crafted ISO/IMG files, path manipulation
4. **Privilege escalation** — not applicable (we already require admin)

---

## Current Safeguards

### Drive Selection
- `device_get_info()` verifies drive existence before any operation
- Non-removable drives are rejected by default (override with `--force`)
- `--list` shows all detected drives with indices before any operation
- User must explicitly confirm with `[y/N]` prompt before destructive write

### Volume Locking
- `device_lock()` dismounts all volumes on the target physical drive
- `FSCTL_LOCK_VOLUME` prevents other processes from holding open handles
- This prevents filesystem corruption from concurrent access

### Image Probing
- `image_probe()` classifies incoming images
- `image_probe_dd_safe()` returns false for pure ISO9660 (optical-only)
  images, refusing raw write with a clear error message
- This prevents users from accidentally writing an optical ISO directly
  to a USB device (which would produce an unbootable result)

---

## Known Security Gaps

### 1. No Drive Index Boundary Check
**File:** `main.c:409-414`

Current code checks `cfg.drive_index < 0` but does not verify that the index
is below `devlist.count`. A user could specify `--list`, count 3 drives
(indices 0-2), then pass `-d 5`. The call to `device_get_info(5)` would
fail, but only at the info retrieval stage — after privilege check and
enumeration.

**Risk:** Low — results in a clear error message, not arbitrary write.
**Fix:** Validate `cfg.drive_index < devlist.count` before proceeding.

### 2. --force Bypasses Removable Check
**File:** `main.c:423-429`

The removable media check is skipped entirely when `--force` is passed.
A user running `bufus.exe -s image.iso -d 0 --force` could target their
system drive.

**Risk:** High — data destruction on system drive.
**Mitigation:** Keep `--force` with clear warning messages. Consider adding
a secondary confirmation for fixed drives even with `--force`. Add drive
model name to the confirmation prompt.

### 3. Source File Path Handling
**File:** `main.c:107`, `bufus.h:46`

`strncpy` is used without guaranteed null termination when the source path
is >= 259 characters. While the buffer is sized at `BUFUS_MAX_PATH_LEN` (260),
`strncpy` does NOT null-terminate if source is >= destination size.

**Risk:** Medium — potential buffer overrun in subsequent string operations.
**Fix:** Explicitly null-terminate after `strncpy`:
```c
cfg.source[BUFUS_MAX_PATH_LEN - 1] = '\0';
```

### 4. Log File Path Injection
**File:** `main.c:135`

Same `strncpy` issue applies to `log_file`. Additionally, no validation is
performed on the log file path — a user could specify paths outside the
intended directory.

**Risk:** Low — log file writing, not device I/O.
**Fix:** `strncpy` + explicit null termination.

### 5. No Integrity Check on Source File

Source files (ISO/IMG) are read without any hash or signature verification.
A corrupted or maliciously modified ISO will be written to the device as-is.

**Risk:** Medium — depends on trust model. If downloading ISOs from the
internet, integrity should be verified before BUFUS touches the device.
**Mitigation:** Document that users should verify ISO checksums before use.
Future: add `--hash` option (see ROADMAP.md).

### 6. Volume Lock Race Condition

`device_lock()` walks all volumes without holding a global lock. Between
volume enumeration and lock attempts, the volume set could change (e.g., a
USB drive is plugged in or a mount point changes).

**Risk:** Low — worst case is a volume mount/unmount during the lock
operation, which would either succeed or fail gracefully.
**Mitigation:** Acceptable for v0.1. For production, consider a retry loop.

---

## Privilege Requirements

BUFUS requires Administrator (elevated) privileges. This is checked at
startup via `bufus_is_elevated()` using `TokenElevation`. Without elevation:
- `CreateFileA` on `\\\\.\\PhysicalDriveN` will fail with
  `ERROR_ACCESS_DENIED`
- `FSCTL_LOCK_VOLUME` will fail
- The application will display an error and exit — it does not attempt
  auto-elevation via UAC prompt

**Design decision:** Do not self-elevate. The user must explicitly
right-click → "Run as administrator." This gives the user a final
opportunity to cancel before destructive operations.

---

## Data at Rest

BUFUS does not encrypt or obfuscate any data:
- No credential storage
- No license keys
- Source image paths are stored in the `bufus_cfg_t` struct on the stack
- Log files contain plaintext operation details (paths, sizes, timings)

No secrets exist in the application. The security-sensitive operation is
raw disk write, which is protected by the OS handle model and volume locking.

---

## Future Security Work

- **Hash-based source verification** — SHA-256 of source file displayed
  after write, allowing manual comparison with upstream checksums
- **Signed builds** — Authenticode signing of release binaries
- **Better --force guardrails** — Require typing the drive model name
  for fixed drives, not just `[y/N]`
- **Audit log** — Append-only log of all write operations (date, image,
  drive, result) for forensic traceability