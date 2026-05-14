---
name: 🐛 Bug Report
about: Report a bug in BUFUS
title: "[BUG] "
labels: bug
assignees: ''
---

<!--
Before submitting, please check if a similar issue already exists.
If this is a crash or data-related issue, you MUST also complete the
"Reproduction Steps" section fully — we cannot investigate otherwise.

⚠️  BUFUS writes directly to physical devices. If you experienced data loss,
please note that in this report but understand that the project maintainer
cannot be held responsible for data loss resulting from misuse of disk
imaging tools. Always double-check the target device before writing.
-->

## Summary

<!-- A clear, concise description of the bug. -->


## Expected Behavior

<!-- What you expected to happen. -->


## Actual Behavior

<!-- What actually happened. Include error messages, codes, or screenshots. -->


## Reproduction Steps

<!--
Complete ALL applicable fields. Incomplete reports will be closed.
-->

### Environment

- **BUFUS version/commit:** <!-- e.g. v0.3.1 or a7f3c2d -->
- **Windows version:** <!-- e.g. Windows 10 22H2 (10.0.19045) or Windows 11 24H2 -->
- **Architecture:** <!-- x86_64 (64-bit) or x86 (32-bit) -->
- **Build method:** <!-- MSYS2/MinGW native, or pre-built release binary -->
- **Administrator privileges:** <!-- Yes/No — BUFUS requires admin to access physical disks -->

### System & Device

- **Target USB device:** <!-- e.g. SanDisk Ultra 32GB, /dev/sdX or \\.\PhysicalDriveN -->
- **Target device size:**
- **USB controller type:** <!-- e.g. USB 3.0 (xHCI), USB 2.0 (EHCI) — check Device Manager -->
- **Source ISO/image file:** <!-- File name and size, e.g. ubuntu-24.04.2-desktop-amd64.iso (4.7 GB) -->
- **Filesystem selected:** <!-- FAT32, NTFS, DD Image, etc. -->

### Issue Details

1. Open BUFUS
2. Select source ISO: `<!-- path to ISO -->`
3. Select target device: `<!-- drive letter or physical drive identifier -->`
4. Click "Start" (or describe the action taken)
5. Observe: `<!-- what happens — error message, hang, crash, wrong output, etc. -->`

<!-- If the bug involves a crash, please also provide: -->
<!-- - Any crash dialog text or Windows Event Viewer entry (Application log) -->
<!-- - If compiled locally, run from a terminal and paste console output -->

### Additional Context

<!--
Screenshots, log output, event logs, or any other relevant information.
If BUFUS has a --verbose or debug mode, please include that output.
-->