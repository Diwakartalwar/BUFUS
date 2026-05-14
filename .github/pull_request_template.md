## Description

<!--
Brief summary of what this PR does.
If it fixes an issue, reference it here: Fixes #NNN
-->


## Type of Change

<!-- Delete non-applicable options -->
- [ ] 🐛 Bug fix
- [ ] ✨ Feature addition
- [ ] 📝 Documentation / comments
- [ ] 🏗️ Refactoring (no behavior change)
- [ ] 🧪 Test addition / improvement
- [ ] 🔧 Build system / CI
- [ ] 📋 RFC / Design proposal

## Pre-Submission Checklist

- [ ] Code compiles with MinGW-w64 (`make` in MSYS2) without warnings at `-Wall -Wextra`
- [ ] Tested on a **non-production** USB device (not a system drive)
- [ ] No hardcoded paths or drive letters remain in the code
- [ ] New code follows existing naming conventions and code style
- [ ] Unsafe disk operations (device open, write, flush, close) include
      proper error handling and cannot silently fail
- [ ] UI strings are in English and under 120 characters (for layout fit)

## Testing Performed

<!--
Describe what you tested. Be specific:
- Which OS version(s)?
- Which USB device(s) / controller(s)?
- Which ISO(s) / image(s)?
- Did verification pass?
- Any edge cases? (small drives, large ISOs, no internet, etc.)
-->


## Notes for Reviewers

<!--
Anything the reviewer should pay special attention to:
- Performance-sensitive paths
- Device I/O logic
- Memory management (no leaks, proper cleanup on error)
- Registry changes (if any)
- UAC/admin privilege handling
-->