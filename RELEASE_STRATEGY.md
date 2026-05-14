# Release Strategy

## Versioning

BUFUS uses [SemVer](https://semver.org/) with one modification: the major
version stays at 0 until the CLI is considered stable and reliable.

```
0.x.y  — Pre-1.0 (current phase)
  x changes frequently, nothing is guaranteed stable
  y increments with each release

1.x.y  — Post-1.0
  x increments with breaking API changes or major feature additions
  y increments with backward-compatible additions or bug fixes
```

## Pre-Release Tags

```
v0.1.0-alpha.x   — Early alpha, known bugs, missing features
v0.1.0-beta.x    — Feature complete for v0.1, testing phase
v0.1.0           — First stable CLI release
v0.2.0-alpha.x   — Linux support development
```

## Release Criteria

### Alpha → Beta

- [ ] All issues tagged `critical` in KNOWN_ISSUES.md resolved
- [ ] `Makefile` exists with working `make release` target
- [ ] Binary size < 300 KB (stripped, release build)
- [ ] Build succeeds on a fresh MSYS2 environment
- [ ] `bufus.exe --list` enumerates all drives correctly on Windows 10 and 11
- [ ] `bufus.exe --benchmark` completes successfully
- [ ] Write + verify round-trip succeeds on a test USB drive (data matches)
- [ ] Log file output matches console output

### Beta → Stable (v0.1.0)

- [ ] All issues tagged `high` in KNOWN_ISSUES.md resolved
- [ ] Error retry logic implemented and tested
- [ ] Buffer pool integration complete and tested
- [ ] Hash verification implemented (`--hash`)
- [ ] Post-write summary statistics added
- [ ] Tested by at least 3 external users on different hardware
- [ ] No memory leaks detected under Valgrind-equivalent Windows analysis
- [ ] README.md completed with installation and usage instructions

### Pre-1.1 (GUI Alpha)

- [ ] Stable CLI v0.1.0 released
- [ ] GUI code separated into `gui/` directory
- [ ] Native Win32 dialog (no frameworks)
- [ ] Drag-and-drop functionality
- [ ] All existing CLI tests pass with GUI codepath

## Binary Distribution

### Contents of Each Release

```
bufus-0.1.0.zip
├── bufus.exe              # Stripped release binary
├── LICENSE                # MIT License
├── README.md              # Quick start guide
├── CHANGELOG.md           # What changed in this release
└── DOCUMENTATION/         # Copies of all .md files
    ├── ARCHITECTURE.md
    ├── ROADMAP.md
    ├── SECURITY.md
    └── ...
```

### Build Process

1. `make release` produces a stripped binary with `-Os -s -static`
2. Binary is verified with `sigcheck` (Sysinternals) for correct PE headers
3. Binary size is checked against the 300 KB target
4. Binary is tested on a clean Windows VM (no dev tools installed)
5. ZIP is created with the contents above
6. SHA-256 checksum is generated for the ZIP
7. Release is tagged and pushed to GitHub

### Signing (Future)

Releases will be Authenticode-signed once a code signing certificate is
obtained. Self-signed test certificates are used during development.

---

## CI/CD (GitHub Actions)

Pending Makefile completion. Planned pipeline:

```yaml
name: Build and Test
on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: msys2/setup-msys2@v2
        with:
          msystem: MINGW64
          install: mingw-w64-x86_64-gcc
      - run: make release
      - run: ./bufus.exe --list  # Smoke test
      - run: ./bufus.exe --benchmark -d 0 2>&1 || true  # May fail in CI
```

The benchmark step is allowed to fail (no physical drive in CI) but the
build and `--list` smoke test must pass.

---

## Changelog Format

Every release includes a `CHANGELOG.md` entry formatted as:

```markdown
## [0.1.0] - 2026-MM-DD

### Added
- Feature description

### Changed
- Modification description

### Fixed
- Bug description

### Known Issues
- Remaining tracked issues
```

Entries follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
format.