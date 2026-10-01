# Changelog

All notable changes to **birun** are documented here.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/).

## [0.8.0] — 2026-01-XX

### Added
- **`timeout(sec)`** task builtin — kills a task's command after N seconds
  (SIGTERM first, then SIGKILL after `--grace`, default 3 s).
- **`retry(n, backoffMs)`** task builtin — re-runs the whole task up to
  `n + 1` times with exponential backoff.
- **Per-task `env("KEY", "VALUE")`** — environment variables scoped to a
  task's child processes. Outside a task, `env("K")` still reads.
- **Per-task `cwd("path")`** — run the task's commands from a chosen
  directory. Outside a task, `cwd()` still returns the current dir.
- **`--stats`** — per-task timings, slowest tasks, and cache hit ratio.
- **`--explain`** — prints *why* each task ran (cache hit / miss reason).
- **`-qq` / `--silent`** — birun prints nothing; only the exit code.
- **`--grace N`** — seconds between SIGTERM and SIGKILL on shutdown/timeout.
- **Verbose timeout diagnostics** — when `--verbose` is set and a command
  times out, birun reports the timeout and grace period used.
- **CI matrix**: Ubuntu (gcc + clang) and macOS (clang).

### Changed
- **Graceful shutdown**: first SIGINT/SIGTERM sends SIGTERM to all children
  and their process groups; after `--grace` seconds a reaper thread sends
  SIGKILL. A second signal forces immediate SIGKILL.
- **Cache file locking**: cache writes now take an advisory `flock` on
  `<cache>.lock`, so two concurrent birun runs can't corrupt the cache.
- **Child registry** grew from 512 → 4096 entries, and is now async-safe.
- **Cache key** now includes `timeout`, `retry`, `env`, and `cwd`, so
  changing these invalidates cached outputs.
- `sh()`, `shStatus()`, `shFull()` now respect the surrounding task's
  timeout / env / cwd, and `shFull()` returns an extra `timedOut` flag.

### Fixed
- `printFail()` now respects `--silent` / `-qq`.
- `runSplit()` no longer leaks pipe fds on early-return paths.

## [0.7.0] — earlier
- Initial public release: task runner + bi language integration,
  caching, parallel execution, watch mode, JSON / Graphviz output,
  shell completions, `--init`.