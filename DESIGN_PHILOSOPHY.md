# Design Philosophy

## Why BUFUS Exists

The USB imaging tool landscape has two extremes:

1. **Enterprise/commercial tools** (Rufus, Win32 Disk Imager GUI) —
   feature-rich but bloated, slow to start, opaque internals
2. **CLI Unix tools** (`dd`, `pv`) — minimal but wrong for Windows
   (no volume locking, no drive enumeration, no safe defaults)

BUFUS sits in the gap: a native Windows CLI tool that does one thing well,
stays under 1 MB, and doesn't hide what it's doing.

---

## Principles

### 1. Small Binary, Big Reach
Every line of code should justify its existence in the final binary.
We target < 1 MB for the GUI release and < 300 KB for CLI. This means:
- No frameworks (Qt, GTK, Electron, .NET)
- No dynamic linking to MSVC runtimes
- No OpenSSL (use native Win32 `BCrypt` when crypto is needed)
- No XML/JSON config files (CLI arguments only)

Binary size is a feature. A small binary downloads faster, fits on a
floppy (well, almost), and is easier to audit.

### 2. Transparency Over Convenience
BUFUS should never hide what it's doing. Every operation is logged.
The progress bar shows real throughput. Errors include the Win32 error
code. The user always knows which physical drive is about to be wiped.

We deliberately avoid:
- Auto-detecting the "correct" drive (let the user choose)
- Silent retries that hide failures
- Confirmation bypasses (except `--yes` for scripting, which is explicit)

### 3. Direct Hardware Access
We talk to devices through Win32 file handles (`\\.\PhysicalDriveN`),
not through filesystem APIs. This means:
- We see exactly the bytes on the device
- We control exactly where they go
- We don't depend on filesystem drivers being correct or present

The tradeoff is that we must handle volume locking, sector alignment,
and hardware quirks ourselves. That's the price of control.

### 4. Fail Loud, Fail Early
If something is wrong, we say so immediately and stop. We do not:
- Continue writing after a failed read hoping it "recovers"
- Silently skip volumes that fail to lock
- Return generic "error" messages when we know the specific cause

Every error code maps to a human-readable string. Every log line has a
timestamp and level. If it fails, you should be able to figure out why
from the log alone.

### 5. No Abstraction for Abstraction's Sake
The module structure is flat and direct:
- `device.c` opens devices, locks them, closes them
- `io.c` reads from source, writes to destination
- `verify.c` reads both, compares bytes
- `disk.c` does disk-specific IOCTL operations

There is no "storage abstraction layer." There is no "device interface."
Each module does the thing its name says, using the minimum API surface
required.

### 6. Correct Before Fast
We write the straightforward, correct version first. Then we optimize
the hot path (the write loop). We do not pre-optimize.

The current write path is synchronous `ReadFile → WriteFile` in a loop.
This is boring. It's also correct. When we make it async, we will do so
with profiling data showing where the time is spent, not speculation.

---

## What We Don't Do

| Anti-pattern | Why not |
|-------------|---------|
| Auto-run on insert | Requires a Windows service/driver. Overkill for a tool. |
| Filesystem-level copy | We're imaging raw devices, not copying files. |
| Compression | CPU overhead for minimal bandwidth savings on fast USB. |
| Multi-platform in v1.0 | Windows first. Get it right on one platform. |
| GUI in v1.0 | CLI forces clarity. GUI comes when the logic is solid. |
| Config files | Everything is CLI args. Config is complexity. |
| Telemetry | We don't phone home. Period. |
| DRM/license checking | It's a free tool. There's nothing to license. |

---

## Naming

BUFUS = Blazing USB Flash Utility System. Yes, it's acronym soup.
The name is intentionally silly — this is a tool, not a product. The
code takes itself seriously. The name doesn't.

---

## Who This Is For

BUFUS is built for people who:
- Want to flash a USB drive from the command line without installing anything
- Need to know exactly what's happening during a write operation
- Care about binary size and dependency footprint
- Prefer to see raw throughput numbers, not "estimated time remaining" guesswork
- Understand that Administrator privileges on Windows mean "this can destroy data"
  and accept that responsibility

If you want a pretty GUI with animated progress bars and one-click ISO
selection, use Rufus. If you want maximum control with minimum overhead,
BUFUS is for you.