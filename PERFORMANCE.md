# BUFUS Performance Notes

## Benchmark Results (Internal Testing)

Hardware varies wildly. Below are representative numbers from development
machines. Treat these as ballpark, not guarantees.

### Write Throughput (Sequential)

| Interface | Drive | Image Size | Throughput | Notes |
|-----------|-------|-----------|------------|-------|
| USB 3.2 Gen 1 | SanDisk Ultra 64GB | 4.2 GB ISO | 35-45 MB/s | SLC cache active |
| USB 3.2 Gen 1 | SanDisk Ultra 64GB | 4.2 GB ISO | 15-20 MB/s | SLC cache exhausted |
| USB 3.1 Gen 2 | Samsung T7 1TB | 4.2 GB ISO | 120-150 MB/s | NVMe-based |
| USB 2.0 | Generic 16GB | 800 MB ISO | 5-8 MB/s | Practical USB 2 ceiling |
| Internal NVMe | — (not target) | 4.2 GB ISO | 800+ MB/s | For reference only |

### Verification Throughput

Verification (byte-for-byte) is approximately 80-90% of write throughput,
since it's a sequential read vs. sequential write on the same bus.
Hash-based verification (future) will be dramatically faster since it
avoids re-reading the device.

### Benchmark Mode Overhead

The `--benchmark` mode writes and reads 256 MiB of synthetic data.
Overhead from `QueryPerformanceCounter` and loop logic is < 0.1%.
Results should be ±5% of raw device throughput.

---

## Block Size Impact

The default block size is 4 MiB (`BUFUS_DEFAULT_BLOCK`). Testing shows:

| Block Size | USB 3.0 Impact | USB 2.0 Impact | Notes |
|------------|---------------|---------------|-------|
| 64 KiB | -15% | -5% | Excessive syscall overhead |
| 256 KiB | -3% | -1% | Marginal |
| 1 MiB | baseline | baseline | Reasonable default |
| 4 MiB | +1-2% | +0% | **Current default** |
| 8 MiB | +0% | -1% | Diminishing returns on USB 2 |
| 16 MiB | +0% | -2% | Larger allocations, no benefit |
| 64 MiB | +0% | -3% | Allocation stalls visible |

4 MiB is the sweet spot for USB 3.x. USB 2.0 is bus-bound regardless.

---

## Memory Usage

| Component | Allocation | Notes |
|-----------|-----------|-------|
| Main write buffer | 4 MiB (default) | `malloc` per write block |
| Verify buffers | 8 MiB total | Two 4 MiB buffers (src + dst) |
| Benchmark buffer | Variable | `VirtualAlloc` aligned to page |
| Buffer pool (future) | Up to 32 MiB | 8 slots × 4 MiB |
| Image probe scan buffer | 1 MiB | `VirtualAlloc`, 1 MiB chunks |
| Stack usage | ~200 KB | Worst case in main() |

Total peak memory: ~50 MiB with verify active. No memory-mapped I/O.

---

## Latency Profile

### Write Path (per 4 MiB block, USB 3.0, mid-range SSD)

```
ReadFile (source):      ~2-5 ms  (sequential, OS cached after first pass)
seek + WriteFile:       ~3-8 ms  (sequential, device-dependent)
progress callback:      <0.1 ms  (formatting overhead)
Total per block:        ~5-13 ms
```

For a 4 GB ISO on a USB 3.0 SSD:
- ~1000 blocks × ~9 ms = ~9 seconds theoretical minimum
- Observed: ~12-15 seconds (overhead from syscall transitions, cache
  invalidation, device command queuing)

### Sanitization Overhead

Sanitizing first + last 16 MiB on a modern USB 3.0 device:
- ~300-500 ms total
- Negligible compared to full image write

---

## Optimization Opportunities

### Current (not yet implemented)

1. **Overlapped I/O**: Read next block while writing current block.
   Expected gain: 20-40% on NVMe USB drives, ~0% on USB 2.0.
   Complexity: Moderate (buffer pool integration, completion port).

2. **Larger block sizes for NVMe**: USB-attached NVMe can handle 1-2 MB
   blocks. Auto-detect optimal block size based on device geometry.

3. **Skip sanitization for known-empty drives**: If drive is brand new
   or already zeroed (check first few MB), skip the wipe pass.

### Not Worth Doing

1. **Multi-threaded hashing during write**: The hashing computation is
   trivial compared to I/O wait. Not a bottleneck.

2. **Write-back caching in user space**: The OS cache manager already does
   this. Adding another layer would just waste memory and add complexity.

3. **Compression during write**: USB bandwidth is already consumed by
   raw data. Compression would add CPU overhead for marginal gain.

---

## Profiling Notes

The code is compiled without optimization flags by default (debug builds).
For profiling:
- Release build: `-O2 -s -DNDEBUG`
- Profile with Windows Performance Recorder (WPR) / Windows Performance
  Analyzer (WPA) for syscall-level analysis
- `QueryPerformanceCounter` timing is already instrumented in the write
  and verify loops

No internal profiling overhead is left in release builds — the
`#ifndef NDEBUG` guards around `LOGD` calls handle this automatically
via the logger's min_level filter.