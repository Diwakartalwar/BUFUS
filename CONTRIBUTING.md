# Contributing to BUFUS

Thanks for your interest. This is a solo-maintained project in early alpha.
Before contributing, please read this document — it explains how the project
is structured, what we need help with, and how to submit changes.

## Prerequisites

- **Build environment:** MSYS2 with MinGW-w64 GCC (`mingw-w64-x86_64-toolchain`)
- **Test environment:** Windows 10/11 with a spare USB drive you don't mind wiping
- **No other dependencies** — the project links against Win32 API and the
  MSVC-compatible CRT bundled into MinGW statically

## Getting Started

### Build

A proper Makefile is needed. Until it lands, build manually:

```bash
gcc -O2 -s -static -o bufus.exe \
    src/main.c src/device.c src/io.c src/disk.c \
    src/verify.c src/benchmark.c src/image_probe.c \
    src/logger.c src/ui.c src/buffer_pool.c \
    -I include -lwinmm
```

(The `-lwinmm` is for `timeGetTime` if needed; currently all timing uses
`QueryPerformanceCounter`, so it may not be strictly required.)

### Verify Build

```bash
./bufus.exe --list
# Should enumerate physical drives without error
```

**Do not test --verify or write operations on a drive with data you care about.**
Use a spare USB drive.

## Code Style

We follow a practical C style. No rigid standard — just be consistent with
the existing code:

- **Indentation:** 4 spaces, no tabs
- **Braces:** K&R style (opening brace on same line)
- **Naming:** `snake_case` for functions and variables, `SCREAMING_SNAKE` for macros
- **Types:** `bufus_err_t`, `device_info_t`, `io_params_t` — project-prefixed,
  typedef'd, no `typedef` in public headers
- **Comments:** Doxygen-style on public API, short inline comments elsewhere
- **Error handling:** Always check return values, always log on failure,
  always clean up before returning error

### Forbidden Patterns

- No C++ features (no STL, no exceptions, no `new`/`delete`)
- No dynamic memory in hot paths (we use `VirtualAlloc` or stack where possible)
- No `#pragma once` — use traditional include guards
- No `goto` without a label cleanup pattern (`goto fail`)
- No uninitialized variables (compile with `-Wall -Wextra` when possible)

## Directory Structure

```
BUFUS/
├── include/          # Public headers (API contracts)
│   ├── bufus.h       # Core types, error codes, config
│   ├── device.h      # Device enumeration and access
│   ├── disk.h        # Disk geometry and sanitization
│   ├── io.h          # Image read/write interface
│   ├── verify.h      # Verification interface
│   ├── benchmark.h   # Benchmark interface
│   ├── image_probe.h  # Image type detection
│   ├── logger.h      # Logging interface
│   ├── ui.h          # Terminal UI interface
│   └── buffer_pool.h  # Buffer pool interface
├── src/              # Implementation
│   ├── main.c        # Entry point, CLI, orchestration
│   ├── device.c      # Win32 device open/lock/close
│   ├── disk.c        # IOCTL-based disk operations
│   ├── io.c          # Read/write loop with progress
│   ├── verify.c      # Byte-comparison verification
│   ├── benchmark.c   # Throughput measurement
│   ├── image_probe.c # Image classification heuristics
│   ├── logger.c      # File/console logging
│   ├── ui.c          # ANSI-colored terminal output
│   └── buffer_pool.c  # Pooled buffer management
├── plan.md           # Project vision and objectives
├── ARCHITECTURE.md   # Module structure and data flow
├── ROADMAP.md        # Future plans
└── ...               # Other docs
```

## How to Contribute

### Reporting Bugs

Open an issue with:
1. What you were doing (command + arguments)
2. What happened (error message or crash)
3. Windows version and drive model (if relevant)
4. Log file output (if `--log` was used)

Bug reports without reproduction steps will be closed with a request for more
detail. This isn't bureaucracy — with raw disk operations, the difference
between "it failed" and "it failed on a Samsung T7 with FAT32" is everything.

### Code Contributions

1. Open an issue first for anything non-trivial. We'd rather discuss
   approach before you write 200 lines that don't match the design.
2. Fork, branch, commit with conventional messages.
3. Pull request with:
   - Description of what changed and why
   - List of files modified
   - Testing notes (what hardware/OS tested on)

### What We Need Help With

| Area | Priority | Notes |
|------|----------|-------|
| Build system (Makefile) | HIGH | Currently manual compile only |
| Async I/O integration | HIGH | Wire buffer_pool into write path |
| Hash verification (SHA-256) | MEDIUM | Use Win32 `BCrypt` API, no OpenSSL needed |
| Error retry logic | MEDIUM | 3-retry with backoff on IO failures |
| Post-write summary | LOW | Time, throughput, byte count |
| Testing on real hardware | HIGH | We need testers with various USB controllers |

### What We Do NOT Need

- GUI proposals (not until v1.0 CLI is stable)
- Cross-platform patches (Phase 2+ in roadmap)
- Feature requests already listed in ROADMAP.md
- Pull requests that add dependencies — BUFUS must remain zero-dependency

## Communication

Issues and PRs are the primary communication channel. No mailing list, no
Discord, no Slack. Check existing issues before opening a new one.

## Licensing

See LICENSE file. Currently MIT.