# birun

> A tiny, fast, cross-platform task runner powered by the [`bi`](https://github.com/barshansarkar/bi) language.

[![CI](https://github.com/barshansarkar/birun/actions/workflows/ci.yml/badge.svg)](https://github.com/barshansarkar/birun/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-1.0.0-blue)](https://github.com/barshansarkar/birun/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-linux%20%7C%20macos%20%7C%20bsd%20%7C%20windows-lightgrey)](#platform-support)
[![Tests](https://img.shields.io/badge/tests-64%2F64-brightgreen)](#testing)

`birun` is a task runner designed for developers who want the power of a real scripting language with the simplicity of `make` / `just` / `Taskfile` — without the ceremony.

```bi
route TASK "/build" {
    desc("Build the release binary")
    inputs("src/**/*.cpp")
    outputs("build/app")
    run("c++ src/*.cpp -O2 -o build/app")
}

route TASK "/test" {
    desc("Run the test suite")
    depends("build")
    run("./build/app --test")
}
```

```bash
birun build       # build once; cached next time
birun test        # build if needed, then test
birun -j 4 all    # parallel
birun --watch dev # re-run on file change
```

## Table of Contents

- [Why birun?](#why-birun)
- [Features](#features)
- [Install](#install)
- [Quick Start](#quick-start)
- [The bi Language](#the-bi-language)
- [Task Builtins](#task-builtins)
- [System Builtins](#system-builtins)
- [CLI Reference](#cli-reference)
- [Caching](#caching)
- [Parallel Execution](#parallel-execution)
- [Watch Mode](#watch-mode)
- [Integrations](#integrations)
- [Platform Support](#platform-support)
- [Environment Variables](#environment-variables)
- [Architecture](#architecture)
- [Building from Source](#building-from-source)
- [Testing](#testing)
- [Versioning & Stability](#versioning--stability)
- [FAQ](#faq)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

## Why birun?

Most task runners force a choice:

| Tool              | Strengths                  | Limitations                              |
|-------------------|----------------------------|------------------------------------------|
| **Make**          | Powerful, mature           | Arcane syntax, no Windows, tab-sensitive |
| **just**          | Clean syntax               | No caching, no parallel, no native Windows |
| **Taskfile**      | YAML-based, simple         | Painful for complex logic                |
| **npm scripts**   | Familiar for Node          | Only for Node projects, no structure     |
| **Shell scripts** | Flexible                   | No caching, no DAG, poor portability     |

**birun** gives you:

- A real language (`bi`) for tasks — conditionals, functions, loops
- A real DAG — dependencies, parallel execution, first-failure cancel
- A real cache — input hashing, output verification, atomic writes
- A real cross-platform story — POSIX + native Win32, static binary

No YAML. No tab headaches. No Node required. No Python. No WSL needed.

## Features

### Task Definition

- ✅ `route TASK "/name" { ... }` declarative syntax
- ✅ `desc()`, `depends()`, `run()` — full task metadata
- ✅ `inputs()` / `outputs()` for cache-aware tasks
- ✅ `timeout()` — kill command after N seconds
- ✅ `retry()` — retry whole task with exponential backoff
- ✅ `env()` — per-task environment variables
- ✅ `cwd()` — per-task working directory
- ✅ Multiple `run()` per task, executed in order
- ✅ Dependencies resolved as a DAG (with cycle detection)

### Execution

- ✅ Sequential by default; `-j N` for parallel
- ✅ First-failure cancel — sibling tasks killed on failure
- ✅ `SIGTERM` → grace period → `SIGKILL` escalation
- ✅ Retry with exponential backoff (capped at 30s)
- ✅ Graceful shutdown on Ctrl-C / SIGTERM / SIGHUP
- ✅ `--dry-run` — show plan without executing
- ✅ `--force` — bypass cache and re-run
- ✅ `--no-cache` — disable cache entirely
- ✅ `--clean` — wipe cache file
- ✅ `--explain` — explain why each task ran
- ✅ `--stats` — per-task timings + cache hit ratio

### Caching

- ✅ FNV-1a 64-bit content hashing
- ✅ Glob expansion (`*`, `?`, `**`)
- ✅ Multi-glob per task
- ✅ Output existence verification
- ✅ Hash includes: runs, depends, desc, outputs, env, cwd, timeout, retry, backoff, cache format version
- ✅ Versioned cache format — bump `BIRUN_CACHE_FORMAT` to invalidate
- ✅ Concurrent-safe writes (`flock` / `LockFileEx`)
- ✅ Atomic writes (temp file + rename)
- ✅ Lock retry with backoff (~775 ms total), then throws — never silent overwrite

### Watch Mode

- ✅ `-w`, `--watch TASK` — re-run on file change
- ✅ `inotify` on Linux (event-driven, low CPU)
- ✅ Polling fallback on macOS / BSD / Windows
- ✅ Ignore filters: `.git/`, `node_modules/`, `build/`, `target/`, etc.
- ✅ Debounce: waits for writes to settle before re-running
- ✅ Auto-reloads config on change

### Cross-Platform

- ✅ Linux — native POSIX, production-tested
- ✅ macOS — native POSIX
- ✅ BSD — native POSIX
- ✅ Windows — native Win32 (`CreateProcess` + Job Objects)
- ✅ Static Windows `.exe` — no external DLLs
- ✅ Cross-compile from Linux via MinGW-w64
- ✅ Cross-platform file lock — `flock` / `LockFileEx`
- ✅ Signal safety — `static_assert` guarantees lock-free atomics

### Integrations

- ✅ `--json` — full task graph as JSON
- ✅ `--json TASK` — execution plan for one task as JSON
- ✅ `--graph` — Graphviz DOT output (`birun --graph | dot -Tpng`)
- ✅ `--completion bash|zsh|fish` — shell completion scripts
- ✅ `--init` — create a starter `birun.bi`
- ✅ `--stats` — performance report

### Robustness

- ✅ Zero warnings with `-Wall -Wextra` (GCC, Clang, MSVC, MinGW)
- ✅ No `system()` / `popen()` — everything via `fork/exec` or `CreateProcess`
- ✅ Process-group kill (POSIX) / Job Object kill (Windows)
- ✅ Async-signal-safe handlers — no `printf`, no `malloc`, no locks
- ✅ Proper `BiError` with `file:line:col` for bi errors
- ✅ Graceful handling of malformed cache files

### Developer Experience

- ✅ Tokyo Night color theme
- ✅ Unicode symbols with automatic ASCII fallback
- ✅ `NO_COLOR` / `BIRUN_ASCII` / `BIRUN_CACHE_FILE` environment variables
- ✅ Aligned, human-readable output
- ✅ Clear cache-hit / cache-miss explanations
- ✅ Semantic versioning with formal policy (`VERSIONING.md`)

## Install

### Pre-built Binaries (Recommended)

Download from [Releases](https://github.com/barshansarkar/birun/releases):

| Platform       | File                       |
|----------------|----------------------------|
| Linux x86_64   | `birun-linux-x86_64`       |
| Windows x86_64 | `birun-windows-x86_64.exe` |

Each release ships with SHA256 checksums. Verify with:

```bash
sha256sum -c birun-linux-x86_64.sha256
```

### Linux / macOS / BSD (from source)

```bash
git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build       # installs to /usr/local/bin
```

### Windows (from source)

**Visual Studio 2019+ Developer Command Prompt:**

```powershell
git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# binary: build\Release\birun.exe
```

**MinGW-w64 (MSYS2 bash):**

```bash
git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# binary: build/birun.exe
```

**Cross-compile from Linux:**

```bash
sudo apt install mingw-w64
cmake -S . -B build-win \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
# binary: build-win/birun.exe
```

## Quick Start

### 1. Create a starter config

```bash
cd ~/my-project
birun --init
```

This writes `birun.bi`:

```bi
// birun.bi — birun task runner config
let mgr  = pkg()          // auto-detect: npm / cargo / pip / go / ...
let isCI = inCI()

route TASK "/hello" {
    desc("Say hello")
    run("echo 'hello from birun'")
}

route TASK "/build" {
    desc("Build the project")
    depends("hello")
    if (mgr == "cargo") {
        run("cargo build --release")
    } else if (mgr == "pnpm" || mgr == "npm" || mgr == "yarn") {
        run("npm run build")
    } else {
        run("echo 'no build system detected'")
    }
}

route TASK "/test" {
    desc("Run the test suite")
    depends("build")
    run("echo 'running tests...'")
}

route TASK "/clean" {
    desc("Remove build artifacts")
    run("rm -rf build dist target")
}
```

### 2. List tasks

```bash
birun --list
# or just:
birun
```

### 3. Run a task

```bash
birun hello
birun build
birun test
```

### 4. Parallel + watch

```bash
birun -j 4 all          # run "all" and its deps in parallel
birun --watch test      # re-run test on any file change
```

## The bi Language

`birun.bi` is a real program written in the **bi** language. You get full support for:

### Variables

```bi
let name = "world"
let count = 42
let pi = 3.14159
let enabled = true
let items = [1, 2, 3]
let config = { host: "localhost", port: 8080 }
```

### Functions

```bi
fn greet(who) {
    return "hello, " + who
}

let msg = greet("birun")   // "hello, birun"
```

### Conditionals

```bi
let mgr = pkg()
if (mgr == "cargo") {
    print("rust project")
} else if (mgr == "npm") {
    print("node project")
} else {
    print("unknown")
}
```

### Loops

```bi
for (let i = 0; i < 5; i++) {
    print(i)
}

for (name in ["a", "b", "c"]) {
    print(name)
}
```

### Task Definition

```bi
route TASK "/deploy" {
    desc("Deploy to production")
    depends("test", "build")
    run("scp build/app prod:/opt/app")
    run("ssh prod 'systemctl restart app'")
}
```

### Dynamic Task Generation

```bi
let targets = ["web", "api", "worker"]

for (t in targets) {
    route TASK "/build-" + t {
        desc("Build " + t)
        run("make " + t)
    }
}
```

## Task Builtins

These builtins can only be used inside a `route TASK { ... }` block.

| Builtin   | Signature                                      | Description                             |
|-----------|------------------------------------------------|-----------------------------------------|
| `desc`    | `desc(text)`                                   | Set task description                    |
| `depends` | `depends("a", "b")` or `depends(["a", "b"])`   | Declare dependencies                    |
| `run`     | `run(cmd)`                                     | Add a shell command (executed in order) |
| `inputs`  | `inputs("src/*.cpp")` or array                 | Glob patterns for caching               |
| `outputs` | `outputs("build/app")` or array                | Expected output files                   |
| `timeout` | `timeout(sec)`                                 | Kill command after N seconds            |
| `retry`   | `retry(n, backoffMs)`                          | Retry up to n+1 times                   |
| `env`     | `env("KEY", "VALUE")`                          | Set env var for this task               |
| `cwd`     | `cwd("path")`                                  | Set working directory for this task     |

**Example:**

```bi
route TASK "/integration" {
    desc("Run integration tests")
    depends("build", "db-up")
    inputs("tests/**/*.py", "src/**/*.py")
    outputs("reports/junit.xml")
    timeout(600)
    retry(2, 1000)
    env("PYTHONPATH", "./src")
    cwd("./tests")
    run("pytest --junit-xml=../reports/junit.xml")
}
```

## System Builtins

These builtins work anywhere in `birun.bi`, including inside tasks.

### Command Execution

| Builtin             | Returns | Description                                   |
|---------------------|---------|-----------------------------------------------|
| `sh(cmd)`           | string  | Run, capture stdout. Throws on non-zero exit. |
| `sh(cmd, strict)`   | string  | `strict=false` ignores exit code              |
| `shStatus(cmd)`     | int     | Run silently, return exit code                |
| `shFull(cmd)`       | map     | `{code, out, err, timedOut}`                  |

### Filesystem

| Builtin         | Returns        | Description             |
|-----------------|----------------|-------------------------|
| `which(tool)`   | string or null | Full path of executable |
| `exists(path)`  | bool           | File/dir exists         |
| `glob(pattern)` | array          | Expand glob             |
| `cwd()`         | string         | Current directory       |
| `cwd("path")`   | null           | (inside task) Set cwd   |

### System

| Builtin               | Returns        | Description                    |
|-----------------------|----------------|--------------------------------|
| `os()`                | string         | `linux` / `darwin` / `windows` |
| `arch()`              | string         | `x86_64` / `arm64` / ...       |
| `inCI()`              | bool           | Running in CI                  |
| `env("X")`            | string or null | Read env var                   |
| `env("X", "default")` | string         | With default                   |
| `pkg()`               | string or null | Detected package manager       |

### `pkg()` Detection Order

| File                  | Returns    |
|-----------------------|------------|
| `pnpm-lock.yaml`      | `pnpm`     |
| `pnpm-workspace.yaml` | `pnpm`     |
| `yarn.lock`           | `yarn`     |
| `bun.lockb`           | `bun`      |
| `package-lock.json`   | `npm`      |
| `Cargo.toml`          | `cargo`    |
| `uv.lock`             | `uv`       |
| `poetry.lock`         | `poetry`   |
| `pyproject.toml`      | `pip`      |
| `requirements.txt`    | `pip`      |
| `go.mod`              | `go`       |
| `composer.json`       | `composer` |
| `pom.xml`             | `maven`    |
| `build.gradle.kts`    | `gradle`   |
| `build.gradle`        | `gradle`   |
| `Gemfile`             | `bundler`  |
| `mix.exs`             | `mix`      |
| `pubspec.yaml`        | `pub`      |

## CLI Reference

```text
birun [OPTIONS] [TASK]
```

### Core

| Flag            | Description                                      |
|-----------------|--------------------------------------------------|
| `<task>`        | Run a task (and its dependencies)                |
| `-l, --list`    | List available tasks                             |
| `-n, --dry-run` | Show plan without executing                      |
| `-j, --jobs N`  | Run up to N tasks in parallel (1-64)             |
| `-w, --watch`   | Re-run task on file changes                      |
| `--no-cache`    | Disable caching                                  |
| `--force, -F`   | Force re-run (still updates cache)               |
| `--clean`       | Delete cache file (and run task, if given)       |

### Output Control

| Flag            | Description                               |
|-----------------|-------------------------------------------|
| `-q, --quiet`   | Suppress decoration, errors only          |
| `-qq, --silent` | Absolute silence (exit code only)         |
| `--verbose`     | Extra diagnostics (plan, cache, timing)   |
| `--stats`       | Per-task timings + cache hit ratio        |
| `--explain`     | Explain why each task ran                 |

### Configuration

| Flag                | Description                                      |
|---------------------|--------------------------------------------------|
| `-f, --file <path>` | Use a specific config file (default: `birun.bi`) |
| `--grace N`         | SIGTERM→SIGKILL grace seconds (default: 3)       |

### Integration

| Flag              | Description                                   |
|-------------------|-----------------------------------------------|
| `--json`          | Print task graph as JSON                      |
| `--json <task>`   | Print execution plan for `<task>` as JSON     |
| `--graph`         | Print task graph as Graphviz DOT              |
| `--completion SH` | Print shell completion (`bash`/`zsh`/`fish`)  |
| `--init`          | Create starter `birun.bi`                     |
| `--force-init`    | Overwrite existing `birun.bi`                 |

### Info

| Flag            | Description   |
|-----------------|---------------|
| `-h, --help`    | Show help     |
| `-v, --version` | Print version |

### Exit Codes

| Code    | Meaning                             |
|---------|-------------------------------------|
| `0`     | Success                             |
| `1`     | General error (config, usage, etc.) |
| `2-127` | Task's own exit code                |
| `124`   | Task timed out                      |
| `130`   | Interrupted (SIGINT) / cancelled    |

## Caching

Tasks with `inputs()` are skipped when:

1. Source file hashes are unchanged, **and**
2. Output files listed in `outputs()` exist

### Example

```bi
route TASK "/build" {
    inputs("src/**/*.cpp", "include/**/*.h")
    outputs("build/app")
    run("c++ src/*.cpp -Iinclude -o build/app")
}
```

- First run → full build  
- Second run (no changes) → skipped  
- Change a source file → rebuilt  
- Delete `build/app` → rebuilt  

### Cache Key

The hash includes:

- All `run()` commands
- `desc()`, `depends()`, `outputs()`
- `env()`, `cwd()`, `timeout()`, `retry()`, `retryBackoffMs`
- Content hash of every matched input file (path + content)
- `BIRUN_CACHE_MAGIC` version string

Changing any of these invalidates the cache entry.

### Cache File

- **Location**: `.birun/cache` (override via `BIRUN_CACHE_FILE`)
- **Format**: `BIRUN_CACHE_MAGIC\nkey\tvalue\n...`
- **Locking**: advisory `flock` / `LockFileEx` with retry
- **Writes**: atomic (temp file + rename)

### Cache Invalidation on Upgrade

The cache file begins with a magic version header. When `birun` upgrades and `BIRUN_CACHE_FORMAT` changes, old caches are silently discarded on next run. No manual `--clean` needed.

## Parallel Execution

Run up to N tasks in parallel:

```bash
birun -j 4 all
```

### Behavior

- Independent tasks run concurrently
- Dependencies are respected — a task waits for all its `depends()`
- **First failure cancels everything** — all running siblings receive `SIGTERM` (POSIX) or Job Object kill (Windows)
- Grace period (`--grace`, default 3s) before `SIGKILL`

### Example

```bi
route TASK "/lint"  { run("eslint src") }
route TASK "/types" { run("tsc --noEmit") }
route TASK "/test"  { run("jest") }

route TASK "/ci" {
    depends("lint", "types", "test")
}
```

```bash
birun -j 3 ci    # lint, types, test run concurrently
```

If `lint` fails, `types` and `test` are killed immediately.

The first failing task determines the exit code — subsequent failures caused by cancellation do not overwrite it.

## Watch Mode

```bash
birun --watch test
```

### How it works

1. Build a snapshot of all watched files (mtime map)
2. Wait for events — `inotify` on Linux, polling elsewhere
3. Debounce: wait 300 ms for writes to settle
4. Re-diff and re-run if anything changed
5. Reload `birun.bi` if the config itself changed

### Ignore Filter

These directories are never watched:

```text
.git/   .hg/   .svn/
build/  .build/  dist/
node_modules/   .cache/  .next/
target/  __pycache__/
.venv/  venv/
.idea/  .vscode/
```

### Watched Extensions

Source-like files only: `.bi`, `.sh`, `.c`, `.cpp`, `.h`, `.rs`, `.go`, `.py`, `.js`, `.ts`, `.json`, `.yaml`, `.toml`, and more.

Press **Ctrl-C** to stop.

## Integrations

### JSON Output

```bash
birun --json              # full task graph
birun --json build        # execution plan for `build`
```

### Graphviz

```bash
birun --graph | dot -Tpng -o tasks.png
```

### Shell Completion

```bash
# bash
birun --completion bash | sudo tee /etc/bash_completion.d/birun

# zsh
birun --completion zsh > ~/.zsh/completions/_birun

# fish
birun --completion fish > ~/.config/fish/completions/birun.fish
```

### CI Example (GitHub Actions)

```yaml
- name: Build
  run: birun build

- name: Test
  run: birun -j 4 test

- name: Stats
  run: birun --stats test
```

## Platform Support

| Platform | Status            | Notes                                 |
|----------|-------------------|---------------------------------------|
| Linux    | ✅ Stable         | native, 64/64 tests                   |
| macOS    | ✅ Stable         | POSIX path                            |
| BSD      | ✅ Stable         | POSIX path                            |
| Windows  | ⚠️ Experimental  | cross-compiled; needs field testing   |

### Windows Notes

- Uses `CreateProcess` + Job Objects (no fork emulation)
- Static `.exe` — no external DLLs required
- Console: enables UTF-8 code page + ANSI escape processing
- Shell: `cmd.exe /s /c "..."` — POSIX-style commands must be adapted (`rm` → `del`, `ls` → `dir`, etc.)
- Watch mode: polling only (no `ReadDirectoryChangesW` yet)
- `shFull()` merges stdout+stderr (POSIX version keeps them split)

## Environment Variables

| Variable                      | Effect                                       |
|-------------------------------|----------------------------------------------|
| `BIRUN_CACHE_FILE`            | Path to cache file (default: `.birun/cache`) |
| `BIRUN_ASCII=1`               | Use ASCII symbols instead of Unicode         |
| `NO_COLOR=1`                  | Disable all colors                           |
| `CI` / `CONTINUOUS_INTEGRATION` | Detected by `inCI()`                       |

## Architecture

```text
src/
├── main.cpp              CLI, argument parsing, orchestration
├── executor.{hpp,cpp}    Task DAG, sequential + parallel execution
├── cache.hpp             Input hashing, cache store, glob expansion
├── builtins_birun.hpp    Task + system builtins for `bi`
├── extras.hpp            JSON, Graphviz, completion, --init
├── theme.hpp             Colors, symbols, formatting
├── watch.hpp             File watching (inotify + polling)
├── version.hpp           Version + cache format constants
├── proc.hpp              Dispatcher: POSIX or Windows
└── platform/
    ├── proc_posix.hpp    POSIX: fork/exec, process groups, signals
    ├── proc_win32.hpp    Windows: CreateProcess, Job Objects
    └── fslock.hpp        Cross-platform file lock
```

### Key Design Decisions

- No dependencies beyond C++17 standard library
- No `system()` — always `fork/exec` or `CreateProcess`
- Signal handlers use only lock-free atomics (`static_assert`-guarded)
- Cache file versioned — changes invalidate old caches
- First-failure-wins in parallel execution (CAS semantics)
- Async-signal-safe process tree kill
- Zero warnings with `-Wall -Wextra` on all four compilers

## Building from Source

### Requirements

- C++17 compiler:
  - GCC 9+
  - Clang 10+
  - MSVC 2019+
  - MinGW-w64 (GCC 10+)
- CMake 3.16+
- pthreads (POSIX only, auto-detected)

### Standard Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### With Tests

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBIRUN_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Cross-compile Windows from Linux

```bash
sudo apt install mingw-w64
cmake -S . -B build-win \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
# binary: build-win/birun.exe (static, no DLLs)
```

### Verify Static Linking

```bash
x86_64-w64-mingw32-objdump -p build-win/birun.exe | grep "DLL Name"
# Expect only: KERNEL32.dll, msvcrt.dll (Windows core)
```

## Testing

64 tests, all passing:

| Suite                  | Tests | Coverage                                                             |
|------------------------|-------|----------------------------------------------------------------------|
| `test_builtins.cpp`    | 16    | `sh`, `shStatus`, `shFull`, `which`, `exists`, `glob`, `os`, `arch`, `inCI` |
| `test_cache.cpp`       | 12    | Wildcard, glob, hashing, `outputsExist`, `CacheStore`                  |
| `test_executor.cpp`    | 10    | Plan, cycle detection, dry-run, seq/par success, failure propagation |
| `test_extras.cpp`      | 8     | JSON escaping, Graphviz, completions, `--init`                       |
| `test_watch.cpp`       | 7     | Filters, snapshot, diff (add/change/remove)                          |
| `test_integration.cpp` | 11    | Real binary spawn — CLI flags, exit codes, caching E2E               |

### Run

```bash
ctest --test-dir build --output-on-failure
```

### CI

Multi-OS matrix runs on every push:

- `ubuntu-latest` — build + test
- `macos-latest` — build + test
- `windows-latest` — build + test
- cross-compile — MinGW-w64 static `.exe` verification

## Versioning & Stability

`birun` follows [Semantic Versioning 2.0.0](https://semver.org/).

See `VERSIONING.md` for the full policy.

### Summary

| Axis           | Macro                | Meaning                         |
|----------------|----------------------|---------------------------------|
| CLI / release  | `BIRUN_VERSION_*`    | User-facing `birun --version`   |
| C++ header API | `BIRUN_API_VERSION`  | Source compatibility of `*.hpp` |
| Cache format   | `BIRUN_CACHE_FORMAT` | On-disk layout + hash inputs    |

### 1.0.0 Guarantees

- No breaking changes to `birun.bi` syntax without MAJOR bump
- No breaking changes to CLI flags or exit codes without MAJOR bump
- No silent cache corruption across upgrades
- All POSIX platforms (Linux, macOS, BSD) are stable
- Windows is experimental — issues welcome

## FAQ

**Is this a make replacement?**  
For most projects, yes. `birun` has a DAG, incremental builds via `inputs()` / `outputs()`, and parallel execution — the core of what `make` does. Where it differs: `birun` uses a real language instead of tab-sensitive rules.

**Why not just just?**  
`just` is excellent for running commands, but has no caching, no DAG-level parallelism, and no cross-platform `.exe`. `birun` adds all three.

**Why not Taskfile?**  
Taskfile is great. `birun` adds: a real language (conditionals, functions, loops for dynamic task generation), signal-safe process management, and native Windows support without a Go toolchain.

**Why not npm scripts?**  
`birun` is language-agnostic. Use it for Rust, C, Python, Go, or mixed projects — no Node.js required.

**Can I use birun in CI?**  
Yes. `birun -j 4 test` works well. Use `--stats` to see timings.

**How do I debug why a task ran?**

```bash
birun --explain --verbose build
```

Prints every cache miss reason and every command being executed.

**Can I share the cache across machines?**  
Not yet. Remote cache support is planned.

**Windows: my clean task fails**  
Windows uses `cmd.exe`, not `sh`. Replace `rm -rf build` with:

```text
run("if exist build rmdir /s /q build")
```

Or use a POSIX shell if you have one (Git Bash, MSYS2):

```text
run("sh -c \"rm -rf build\"")
```

**Windows: colors look weird**  
Windows 10+ console supports ANSI escapes. `birun` enables them via `SetConsoleMode`. On older consoles, set `NO_COLOR=1`.

## Roadmap

- [ ] Native Windows watch (`ReadDirectoryChangesW`)
- [ ] Remote cache backend (S3/HTTP)
- [ ] Plugin system for custom builtins
- [ ] `birun --svg` — inline SVG task graph
- [ ] Homebrew formula
- [ ] Scoop / winget package
- [ ] AUR package
- [ ] `.deb` / `.rpm` packages

## Contributing

Issues and PRs welcome. Before submitting:

1. Run `cmake --build build -j` — must be zero-warning
2. Run `ctest --test-dir build --output-on-failure` — must be 64/64
3. Update `CHANGELOG.md` if behavior changes
4. Add tests for new features

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgements

- [bi](https://github.com/barshansarkar/bi) — the scripting language
- Tokyo Night color theme
- Catch2 for testing
- Everyone who filed issues

---

Made with 🖤 by [@barshansarkar](https://github.com/barshansarkar)
```