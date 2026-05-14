# Why BUFUS

## The Problem

You need to write an ISO image to a USB drive on Windows. You want it to:
- Be bootable afterwards
- Not corrupt your existing data
- Tell you what it's doing
- Not install anything, require an account, or phone home

Your options today:

**Rufus** — Excellent tool, but it's a GUI application with 5 MB+ of
dependencies. If you're working on a headless server or scripting deployments,
you're out of luck.

**Etcher** — Electron-based. 100+ MB download. Writes correctly but
slower than it should be, and the UI hides the details of what's happening
under the hood.

**Win32 Disk Imager** — Lightweight but unmaintained. Last stable release
predates UEFI boot standards. No verification mode, no command-line
interface.

**`dd` on Windows (Cygwin/MSYS2)** — Works in theory. In practice, it
doesn't handle Windows drive locking, doesn't dismount volumes before
writing, and can silently produce corrupt results while reporting success.

None of these tools are wrong. They just don't match every use case.

## What BUFUS Does Differently

1. **CLI-first.** Every operation is a command-line flag. This means you can
   script it, put it in a deployment pipeline, or run it over SSH. The GUI
   comes later (planned for v1.1), but the CLI is the primary interface.

2. **Small.** The binary targets < 1 MB for the full release. No frameworks,
   no runtimes, no DLLs. It runs on any Windows 10/11 machine with nothing
   else installed.

3. **Native.** Win32 API only. No abstraction layers over the disk I/O.
   We open the physical drive directly, lock the volumes, write the bytes,
   and verify them. What you see in the progress bar is what actually
   happened at the disk level.

4. **Safe.** Before writing anything, BUFUS:
   - Requires Administrator privileges (no silent elevation)
   - Verifies the image type (won't write a non-bootable optical ISO as raw)
   - Locks and dismounts all volumes on the target drive
   - Sanitizes the partition table area before writing
   - Optionally verifies the written data byte-for-byte

5. **Transparent.** Every operation is logged with timestamps. Errors
   include the specific Win32 error code. You can enable debug logging
   with `--verbose` and redirect output to a file with `--log`.

6. **Honest about limitations.** BUFUS doesn't pretend to be a partition
   manager or a multi-boot tool. It writes raw bytes from a file to a
   physical drive. If you need something more sophisticated, use a more
   sophisticated tool.

## When to Use BUFUS

- You're deploying a Linux ISO to multiple USB drives from a script
- You need a lightweight tool on a locked-down Windows machine
- You want to verify the write completed correctly (`--verify`)
- You're curious how fast your USB drive actually is (`--benchmark`)
- You want to understand exactly what's happening during a USB write

## When NOT to Use BUFUS

- You want a GUI (use Rufus for now)
- You need to write a Windows installation USB (use Microsoft's Media
  Creation Tool — it handles activation and updates correctly)
- You need multi-boot on a single USB (use Ventoy)
- You're on Linux or macOS (BUFUS is Windows-only for now)

## The Name

BUFUS stands for Blazing USB Flash Utility System. It's intentionally
over-the-top. The tool itself is serious engineering. The name is not.