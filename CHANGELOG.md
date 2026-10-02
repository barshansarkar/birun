## [1.0.0] — 2026-10-02

First stable release.

### Highlights
- Cross-platform: Linux, macOS, BSD, Windows
- Parallel tasks with first-failure cancel
- Versioned input-hash cache
- Watch mode (inotify on Linux)
- Static Windows `.exe` (no external DLLs)

### Platform status
- Linux/macOS/BSD: **stable**
- Windows: **experimental** (cross-compiled + Wine-tested;
  field testing in progress)

### Tests
- 64/64 pass on Linux (unit + integration)
- CI: multi-OS matrix + MinGW cross-compile

### Fixed
- Parallel failure now cancels all running siblings (CAS-first-fail)
- Cache lock failures retry with backoff, then throw (never silent)
- Signal handlers use only lock-free atomics
- Watch mode uses inotify on Linux (low CPU)

### Added
- `version.hpp` — single source of truth
- `VERSIONING.md` — semantic versioning policy
- `tests/test_integration.cpp` — 11 end-to-end CLI tests
- `src/platform/` — POSIX / Windows split
- Static linking for Windows `.exe`