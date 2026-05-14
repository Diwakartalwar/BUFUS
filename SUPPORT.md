# Support

## Before Asking for Help

BUFUS is an early-stage open-source project maintained by a single developer
in their spare time. Please help us help you by following these steps before
opening an issue:

1. **Search existing issues** — your problem may already be known or have a
   workaround.
2. **Read the README** — build instructions, known limitations, and usage
   notes are documented there.
3. **Run BUFUS from a terminal** — if you're using a pre-built binary, open
   cmd.exe or PowerShell as Administrator and run `bufus.exe` to see
   console output. GUI-only mode may swallow error messages.

## Reporting Bugs

Use the **Bug Report** template (see `.github/ISSUE_TEMPLATE/bug_report.md`).
Incomplete reports — especially those missing environment details or
reproduction steps — will be closed.

**We need at minimum:**
- Windows version and build number (`winver`)
- BUFUS version or commit hash
- Exact steps to reproduce
- Console output or error messages
- USB device model and target drive letter

Please do **not** email the maintainer directly. GitHub Issues is the only
official support channel — this keeps answers searchable for everyone.

## Filing Feature Requests

Use the **Feature Request** template. Keep in mind:
- BUFUS focuses on raw USB imaging and bootable drive creation, not
  general-purpose disk management.
- Feature requests that are well-explained with a clear use case are far more
  likely to be prioritized.

## ⚠️ Data Safety Disclaimer

BUFUS writes directly to physical disk devices. There is no undo. The
project maintainer is not responsible for data loss resulting from selecting
the wrong target device. Always verify:
- The target drive letter or physical drive number is correct
- No important data exists on the target device before writing
- You have backups of anything you cannot afford to lose

## Contributing

Contributions are welcome. See `CONTRIBUTING.md` (if it exists) or the
project's `README.md` for build instructions and contribution guidelines.