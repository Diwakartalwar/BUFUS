# BUFUS — Blazing USB Flash Utility System

## Goal

Create an ultra-lightweight, portable, high-performance USB imaging tool inspired by Rufus.

Primary objectives:

- Portable single EXE
- Under 1 MB release target
- Faster or comparable write speed to Rufus
- Safe verification system
- Windows-first development
- No Electron, .NET, Java, or heavy frameworks
- Native Win32 API only

---

# Philosophy

BUFUS is designed like old-school system utilities:

- Small
- Fast
- Native
- Direct hardware access
- Minimal dependencies
- No unnecessary abstraction

The project prioritizes:

1. Performance
2. Reliability
3. Tiny binary size
4. Portability

---

# Technical Stack

## Language

C (C17)

Reason:
- Extremely small binaries
- Direct Win32 API access
- Maximum I/O control
- No runtime dependency
- Easier static distribution

---

# Compiler

Preferred:
- MinGW-w64 GCC

Optional:
- MSVC

---

# Target Binary Size

| Stage | Expected Size |
|---|---|
| CLI Prototype | 100–300 KB |
| Full CLI | 300–700 KB |
| GUI Release | <1 MB target |

---

# Platform Roadmap

## Phase 1
Windows only

## Phase 2
Linux support

## Phase 3
macOS support

Cross-platform support will only begin after stable Windows implementation.

---

# Core Features

## Required

- Raw USB imaging
- ISO writing
- IMG writing
- Verification mode
- Drive detection
- Progress tracking
- Safe device locking
- Admin privilege enforcement
- Portable executable

---

# Architecture

```text
ISO/IMG File
      ↓
Read Buffer
      ↓
Write Queue
      ↓
WriteFile()
      ↓
USB Device
      ↓
Verification Pass