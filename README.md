# BUFUS — Blazing USB Flash Utility System

BUFUS is a tiny, high-performance USB imaging utility focused on producing portable, minimal, native executables for Windows (with later cross-platform goals).

Project goals
- Portable single executable (no heavy runtimes)
- Tiny binary sizes (under 1 MB target)
- High write/verify performance for ISO/IMG to USB
- Safe verification and device handling

Key facts
- Language: C (C17)
- Primary platform: Windows (Phase 1)
- Build: MinGW-w64 GCC preferred; MSVC optional
- License: CC0 1.0 Universal (public domain dedication)

Why BUFUS
BUFUS follows the old-school systems philosophy: small, fast, native, and direct hardware access with minimal dependencies. If you value speed, tiny binaries, and direct control of I/O, BUFUS is built for you.

Repository layout
- `src/` — core C sources (benchmark.c, buffer_pool.c, device.c, disk.c, io.c, logger.c, main.c, ui.c, verify.c)
- `bench/` — benchmarking helpers
- `tools/` — build and auxiliary tools
- `include/` — public headers

Getting started (quick)
1. Install MinGW-w64 or use MSVC toolchain on Windows.
2. From the repository root, build with your chosen toolchain (example using GCC/MinGW):

```powershell
gcc -O2 -s -o bufus.exe src/*.c -Iinclude
```

3. Run with administrator privileges to access raw device I/O.

Important notes
- Always select the correct target device. Writing to the wrong device will irreversibly overwrite data.
- Running the binary requires elevated privileges on Windows.

Contributing
- Report issues or feature requests via GitHub Issues.
- PRs are welcome — keep changes small and focused. Prefer clarity and portability.

License
This project is dedicated to the public domain under CC0 1.0 Universal. See `LICENSE` for details.

Contact
Maintainer: Diwakartalwar

Thank you for checking out BUFUS — tiny, fast, and focused.
