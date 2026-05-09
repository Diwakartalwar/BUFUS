## Plan: BUFUS — Fast, Cross-Platform Imaging Tool

TL;DR - Build a safety-first, high-throughput imaging tool (BUFUS) with a modular core and per-OS high-performance I/O backends. Start CLI-first, provide a minimal GUI later, and aim for >=25% throughput improvement over Rufus on USB 3.0 flash drives while keeping verification as the default.

**Steps**
1. Define goals & acceptance criteria
   - **Target metric**: >=25% faster than Rufus on a representative set of USB 3.0 flash drives (final numeric targets to be collected during baseline).
   - **Safety**: default mode performs byte-for-byte or checksum verification; provide an opt-in "fast mode" that trades verification for speed.
   - **Scope**: cross-platform (Windows, Linux, macOS) with Windows as the initial engineering focus for parity with Rufus behavior.
   - **Constraints**: user-mode only (no kernel drivers), must require explicit admin privileges for device writes.

2. Project scaffolding (depends on step 1)
   - **Repository**: new repo named `BUFUS` (clean-slate). Use a permissive license (MIT or Apache-2.0).
   - **Language / stack (recommendation)**: Rust for the core (memory safety, cross-platform toolchains), with small C FFI bindings only where necessary (e.g., linking to existing C libraries like wimlib). Alternatives: C/C++ if direct porting of Rufus components is preferred.
   - **Layout**: core library (platform-agnostic pipeline), platform backends (windows, linux, macos), CLI, minimal GUI (Tauri) as a separate workspace crate, tests, and bench harness.

3. Baseline benchmarking (depends on step 1)
   - Build or obtain the latest Rufus build and collect baseline write times on 3–5 representative USB 3.0 flash drives and one NVMe enclosure if available.
   - Define standard test images (e.g., 4GB ISO, compressed WIM, and a raw IMG) and measurement method (sustained MB/s, total time, 95th percentile stalls).

4. Profiling & instrumentation (depends on step 3)
   - Add tracing hooks and timers in the pipeline (read, decompress, enqueue, write, verify).
   - Windows: ETW/WPA traces for the write path; Linux: perf and flamegraphs or io_uring traces; macOS: Instruments.
   - Capture device-level stats (latency, short writes, device-reported sector size).

5. Design the I/O pipeline (depends on step 4)
   - **Architecture**: producer-consumer pipeline: Reader -> Decompressor (optional) -> Buffer Pool -> Writer backend -> Verifier -> Finalizer.
   - **Backpressure**: bounded queues between stages to limit memory and adapt to device slowness.
   - **Buffer pool**: sector-aligned, reusable buffers (alignment to device logical block). Default buffer chunk: 4–16 MB (auto-tuned).
   - **Verification**: streaming checksums (CRC32 or SHA256 depending on user preference) computed concurrently with writes.

6. Platform-specific I/O backends (parallel work possible)
   - **Windows**: IOCP-based overlapped asynchronous writes with `CreateFile` using `FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH` where supported; submit multiple in-flight overlapped writes to saturate device; ensure sector alignment and size multiple constraints; explicit `FlushFileBuffers` on finalize.
   - **Linux**: prefer `io_uring` for high throughput; fallback to `O_DIRECT` with buffered submission using libaio or threaded `pwritev` when `io_uring` unavailable.
   - **macOS**: use `fcntl(F_NOCACHE)` and aligned writes, test `fcntl` advisory flags; consider the `DRIVERKIT`/IOKit path only if necessary (avoid kernel extensions).
   - **Fallbacks**: when direct/unbuffered IO fails, gracefully fall back to buffered WriteFile/pwrite with throttling.

7. Implement core modules (depends on step 5 & 6)
   - `core/pipeline` — pipeline orchestration, metrics, graceful abort
   - `core/buffer_pool` — aligned buffer allocator and recycling
   - `backends/windows/iocp_worker` — IOCP writer implementation
   - `backends/linux/io_uring_worker` — io_uring writer implementation + fallback
   - `decompressor/*` — streaming decompression for gzip/xz/lz4
   - `verifier/*` — parallel hashing utilities
   - `cli/` — CLI to exercise modes, autotune flags, progress output
   - `gui/` — minimal GUI (progress bar, mode toggle, device selector) using Tauri (or optional native shell)

8. Autotuning & heuristics (depends on step 7)
   - Probe device latency and throughput early (micro-benchmarks) and choose buffer size and number of in-flight requests.
   - Heuristics for flash controllers: detect short-write patterns and reduce in-flight concurrency if short writes occur.

9. Safety, UX & modes (parallel)
   - **Default (Safe)**: full verification, more conservative tuning, explicit user confirmations.
   - **Fast Mode (Opt-in)**: verification can be disabled; autotuning to maximize throughput (present clear warnings in UI/CLI).
   - **Resume/abort behavior**: safe abort with clear device flush and rollback semantics where possible.

10. Testing & benchmarks (depends on 7,8,9)
   - Unit tests for buffer pool, verifier, and error handling.
   - Integration tests using loopback devices and VM-attached USB pass-through where possible.
   - Manual bench scripts for Windows (invoke BUFUS and Rufus with the same image/device and collect timings). Example (conceptual):

   - **Windows benchmark idea**: run Rufus to write image then run BUFUS in the same environment and compare total time and sustained MB/s.

11. Packaging, CI, and release (depends on 10)
   - Build artifacts for Windows (MSVC and MinGW as appropriate), Linux, and macOS.
   - CI runs unit tests and cross-compilation; hardware benchmarks remain manual and documented.
   - Document safe/fast modes, supported devices, and known limitations in README.

**Relevant files (new repo structure suggestions)**
- `core/src/lib.rs` — pipeline public API and interfaces.
- `core/src/buffer_pool.rs` — aligned buffer allocator and pool.
- `core/src/pipeline.rs` — stage coordination and metrics.
- `backends/windows/iocp_worker.rs` — Windows IOCP backend.
- `backends/linux/io_uring_worker.rs` — Linux io_uring backend + fallbacks.
- `cli/src/main.rs` — CLI binary and argument parsing.
- `gui/` — minimal Tauri app for progress and mode toggles.
- `bench/` — reproducible benchmark harness and scripts.

**Verification**
1. **Baseline**: record Rufus baseline (per-device) including mean MB/s, total time, and stalls.
2. **Goal**: demonstrate >=25% throughput improvement on at least 3 representative USB 3.0 flash drives while preserving correctness under the default (safe) mode.
3. **Correctness**: verification must detect mismatches; any successful "fast mode" runs should be explicitly marked and not overwrite default safe behavior.
4. **Stability**: repeated runs (5x) without crashes or data corruption; verify abort & retry behaviors.

**Decisions & Assumptions**
- **Language**: Rust recommended for core due to safety and cross-platform tooling; alternate C/C++ path available for direct reuse of Rufus C code if you prefer.
- **Initial focus**: Windows + Linux first, macOS as follow-up to reduce surface area and delivery time.
- **No kernel-mode drivers**: user-mode only to remain distributable and safe.
- **Licensing**: use a permissive license to encourage adoption and forks (MIT or Apache-2.0).

**Risk & Mitigations**
- **Hardware variance**: flash controller behavior widely varies — mitigate by testing many devices and providing per-device heuristics and conservative defaults.
- **Direct I/O pitfalls**: unbuffered IO requires strict alignment; implement robust fallbacks and helpful error messages.
- **CI limitations**: hardware benchmarks are manual — provide clear, reproducible bench scripts and expected output format.

**Further Considerations**
1. Provide thorough documentation on what "fast mode" means and when users should use it.
2. Consider optional telemetry (opt-in) to collect anonymized device performance metrics to improve autotuning.
3. Explore partnering with wimlib and other existing projects for reusing decompression/streaming code.

**Next steps (short-term)**
- Create the `BUFUS` repository and push initial README and scaffolding (core, backends, cli, bench).
- Implement a minimal Windows IOCP prototype that writes a pre-allocated aligned buffer repeatedly to a loopback device and measure baseline throughput.
- Run baseline Rufus benchmarks on one USB 3.0 flash drive and record results.You've used 67% of your session rate limit. Your session rate limit will reset on May 10 at 12:06 AM. [Learn More](https://aka.ms/github-copilot-rate-limit-error)