# Device Compatibility

## Tested Hardware

The following devices have been tested with BUFUS during development.
"Tested" means the drive was enumerated, opened, locked, written to,
and verified successfully.

| Drive | Interface | Capacity | Firmware | Result | Notes |
|-------|-----------|----------|----------|--------|-------|
| SanDisk Ultra Fit | USB 3.2 Gen 1 | 64 GB | S905X7 | ✅ Pass | SLC cache visible at 40 MB/s, drops after ~2 GB |
| Samsung T7 | USB 3.1 Gen 2 | 1 TB | MU01 | ✅ Pass | Consistent 130+ MB/s, NVMe internals |
| SanDisk Extreme Pro | USB 3.2 Gen 1 | 256 GB | S912 | ✅ Pass | Write speeds drop significantly after cache exhaustion |
| Generic 16 GB USB 2.0 | USB 2.0 | 16 GB | Unknown | ✅ Pass | ~6 MB/s write, bus limited |

## Known Problematic Hardware

| Drive | Interface | Issue | Workaround |
|-------|-----------|-------|------------|
| Some Kingston DataTraveler models | USB 3.0 | Lock fails intermittently | Retry with `--force` |
| Certain no-name Chinese USB 3.0 sticks | USB 3.0 | Report incorrect sector size (4096 instead of 512) | None yet — causes misaligned writes |

### Workaround
If your device isn't listed, run `--benchmark` first to check if basic
read/write works. If benchmark passes but write fails, it's likely a
locking issue — try closing all file explorers and antivirus before
retrying.

---

## Enclosure Chipsets

USB-SATA/NVMe enclosures use bridge chipsets that can affect behavior:

| Chipset | Known Behavior | Notes |
|---------|---------------|-------|
| ASMedia ASM1153E | Works fine | Common in USB 3.0 SATA enclosures |
| JMicron JMS580 | Works fine | USB 3.1 enclosures |
| Realtek RTS5768DL | Works fine | Common in NVMe enclosures |
| VL716 (Via Labs) | May have TRIM issues | TRIM passthrough not tested |

### USB Attached SCSI (UAS)

BUFUS does not use the UAS driver. If Windows loads UAS for your device,
raw SCSI commands may behave differently. The drive still appears as
`\\.\PhysicalDriveN` regardless of the storage driver in use.

To check which driver your device uses:
```
devcon driverdevices USBSTOR
```

If UAS is active and you see write corruption, try disabling it:
```
devcon install USBSTOR <device_hwid>
```
This forces the device to use the bulk-only transport (BOT) driver instead.

---

## Capacity Limits

| Capacity | Status | Notes |
|----------|--------|-------|
| < 32 GB | ✅ Fully supported | Standard USB flash |
| 32 GB – 2 TB | ✅ Fully supported | Most portable SSDs fall here |
| 2 TB – 16 TB | ⚠️ Tested minimally | Large spinning USB disks work, but sanitization time increases |
| > 16 TB | ❌ Not tested | `uint64_t` can handle it, but no real-world testing |

---

## Virtual Drives

| Virtual Drive | Status | Notes |
|---------------|--------|-------|
| ImDisk virtual disk | ⚠️ Partial | Appears as PhysicalDrive but locking may fail |
| VHD/VHDX mounted as physical | ❌ Not supported | Windows mounts these as volumes, not physical drives |
| iSCSI targets | ⚠️ Partial | Works if exposed as PhysicalDrive; depends on iSCSI initiator |

---

## Filesystem Considerations

BUFUS writes raw bytes. The target drive's existing filesystem is irrelevant
— it's wiped during sanitization. However, the filesystem on the *source*
image matters for bootability:

| Source Format | Writeable? | Bootable? | Notes |
|---------------|-----------|-----------|-------|
| Raw disk image (.img) | ✅ Yes | ✅ If image was bootable | Direct sector copy |
| Hybrid ISO (.iso) | ✅ Yes | ✅ Yes | MBR + ISO9660 coexist |
| Pure ISO9660 (.iso) | ⚠️ Blocked by default | ❌ Not via raw write | Use `Etcher` or mount + copy instead |
| FAT32 filesystem image | ✅ Yes | ✅ If bootloader present | Detected as disk image |
| NTFS filesystem image | ✅ Yes | ⚠️ Depends on boot config | Windows PE images work |