<div align="center">

# birun

**A lightweight, high-performance task runner powered by a compact language called `bi`.**

*Write build steps as real code. Skip unchanged work. Execute the rest in parallel.*

[![CI](https://github.com/barshansarkar/birun/actions/workflows/ci.yml/badge.svg)](https://github.com/barshansarkar/birun/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/barshansarkar/birun?color=c25a3c)](https://github.com/barshansarkar/birun/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-d4a35c.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-8ba368)]()

</div>

---

## Table of Contents

- [Overview](#overview)
- [Motivation](#motivation)
- [Key Differentiators](#key-differentiators)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [The `bi` Language](#the-bi-language)
- [Defining Tasks](#defining-tasks)
- [Task Builtins](#task-builtins)
- [System Builtins](#system-builtins)
- [Caching](#caching)
- [Parallel Execution](#parallel-execution)
- [Watch Mode](#watch-mode)
- [CLI Reference](#cli-reference)
- [Integration](#integration)
- [Environment Variables](#environment-variables)
- [Comparison with Other Tools](#comparison-with-other-tools)
- [Building from Source](#building-from-source)
- [Architecture](#architecture)
- [Testing](#testing)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

`birun` is a task runner. You describe what you want to do — build, test, lint, deploy — in a file named `birun.bi`. `birun` then determines the correct execution order, runs independent tasks in parallel, and skips any task whose inputs have not changed since the last successful run.

What distinguishes `birun` from other task runners is the language you write your tasks in. Not YAML. Not a Makefile. Not a shell script with naming conventions. You write tasks in **`bi`** — a small scripting language with variables, conditionals, loops, and functions, and nothing else. It fits on two screens, can be learned in an afternoon, and allows you to express the kinds of decisions that cause YAML configurations to sprawl.

`birun` ships as a single statically-linked binary. No runtime to install, no package manager to invoke, no service to start. Approximately 200 KB stripped.

---

## Motivation

Every project eventually accumulates a `scripts/` directory. It begins with a single six-line file that is perfectly readable. Then someone adds a flag, someone adds a dependency, and by the third month there are fourteen files, each sourcing the last, each half-broken, and nobody remembers which one is authoritative.

Teams migrate to `Makefile` and discover tabs. They move to `package.json` scripts and discover their build logic is now JSON. They adopt `just` and everything works until someone needs a conditional. Then someone writes a wrapper script that calls `just`, which calls a shell script, which calls a Python script. The whole thing works, but nobody wants to touch it.

The problem is always the same: you either get a configuration language too rigid for real logic, or a general-purpose language too heavy for build steps. `birun` occupies a middle path — a small DSL designed specifically for describing what a build does.

---

## Key Differentiators

| Capability | Description |
| --- | --- |
| **Real conditionals, loops, and functions** | Your build file is code. Branch on environment, iterate over files, factor repeated logic into named functions. |
| **Content-hash caching** | Make-style timestamp caching breaks on `git clone`, backup restore, and branch switch. `birun` hashes file contents along with the task definition. If a byte changed, the task reruns. If nothing changed, it does not. |
| **True parallel execution** | `-j8` runs eight tasks on eight threads. Output is buffered per task and flushed atomically, so parallel runs do not garble the terminal. |
| **One file, no runtime** | No Node, no Python, no JVM. Works in minimal containers, embedded systems, and CI runners that only guarantee `sh` and `coreutils`. |
| **Filesystem-aware watch mode** | Ignores `.git`, `node_modules`, `target`, `build`, `.venv`, `__pycache__`. Watches only relevant extensions. Debounces rapid saves. Reloads configuration on change. |
| **Honest failure messages** | No stack traces to decode. Errors state what happened, where, and why. |

---

## Installation

### Linux and macOS — One-Line Install

```sh
curl -fsSL https://raw.githubusercontent.com/barshansarkar/birun/main/install.sh | sh
```

Downloads the correct binary for your platform and installs it to `~/.local/bin/birun`. If that directory is not on your `PATH`, the installer explains how to add it.

**Supported platforms:** Linux x86_64, Linux aarch64, macOS x86_64 (Intel), macOS arm64 (Apple Silicon).

### Homebrew

```sh
brew install barshansarkar/tap/birun
```

### From Source

Requires CMake 3.16+ and any C++17 compiler.

```sh
git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

### Verify Installation

```sh
birun --version
# birun 0.6.2
```

---

## Quick Start

```sh
cd my-project
birun --init              # writes a starter birun.bi
birun --list              # see what tasks exist
birun build               # run the build task and its dependencies
birun -j4 test            # run tests with four parallel workers
birun --watch test        # re-run on file changes
```

That is the workflow. The remainder of this document explains each piece.

---

## The `bi` Language

`bi` is deliberately small. If you have written JavaScript, Python, or Ruby for a week, you already know how to read it.

### Values

```bi
42              // integer
3.14            // floating point
"hello"         // string
true            // boolean
null            // absence
[1, 2, 3]       // array
{ a: 1, b: 2 }  // map (insertion-ordered)
```

### Variables

```bi
let name = "world"
let count = 42
let cfg = { debug: true, level: 3 }
```

Lexically scoped. `let` shadows in a child scope rather than mutating an outer binding.

### Operators

```bi
+ - * / %       // arithmetic
== != < <= > >= // comparison
&& ||           // logical, short-circuiting
!               // not
+= -= *= /=     // compound assignment
```

String concatenation uses `+`:

```bi
let msg = "hello, " + name
```

### Conditionals and Loops

```bi
if (count > 10) {
    print("large")
} else if (count > 0) {
    print("small")
} else {
    print("non-positive")
}

for (let i = 0; i < 5; i = i + 1) { print(i) }

for (item in ["a", "b", "c"]) { print(item) }

for (key in { x: 1, y: 2 }) { print(key) }

for (ch in "hello") { print(ch) }
```

`break` and `continue` work as expected.

### Functions

```bi
fn greet(who) {
    return "hello, " + who
}

let double = fn(x) { return x * 2 }   // anonymous
```

Functions close over their lexical environment. Nested functions can reference outer variables.

### Errors

```bi
try {
    risky_thing()
} catch (e) {
    print("failed: " + e)
}
```

`catch` also intercepts runtime errors from builtins.

---

## Defining Tasks

A task lives inside `route TASK "/name" { ... }`. The name is the identifier you use on the command line.

```bi
route TASK "/build" {
    desc("Compile the project")
    inputs("src/**/*.rs", "Cargo.toml")
    outputs("target/release/app")
    depends("generate-protos")
    run("cargo build --release")
}
```

The block is real code:

```bi
let mgr  = pkg()
let isCI = inCI()
let profile = env("PROFILE", "release")

route TASK "/build" {
    desc("Build the project")
    inputs("src/**/*")
    outputs("target/" + profile)

    if (mgr == "cargo") {
        run("cargo build --" + profile)
    } else if (mgr == "npm" || mgr == "pnpm" || mgr == "yarn") {
        run("npm run build")
    } else if (mgr == "go") {
        run("go build ./...")
    } else {
        throw "unknown package manager: " + mgr
    }

    if (isCI) {
        run("echo 'running in CI mode'")
    }
}
```

Tasks depend on other tasks. `birun` sorts them topologically, detects cycles, and reports the exact path.

```bi
route TASK "/ci" {
    desc("Full CI pipeline")
    depends("lint", "test", "typecheck")
}
```

With `-j4`, up to four ready tasks run at once.

---

## Task Builtins

These are valid only inside a `route TASK` block.

### `desc(text)`

Human-readable description shown in `--list`. Only the first call has an effect.

### `depends(name, ...)`

Declares dependencies. Accepts strings or arrays.

```bi
depends("build")
depends("lint", "typecheck")
depends(["build", "generate"])
```

### `run(command)`

Adds a shell command. Commands run in declaration order. A non-zero exit fails the task.

```bi
run("cargo build --release")
run("strip target/release/app")
```

### `inputs(glob, ...)`

Declares files whose contents determine whether the task needs to rerun. Accepts `**` for recursive matching, and arrays.

```bi
inputs("src/**/*.rs", "Cargo.toml", "Cargo.lock")
```

No inputs means the task is never cached — it runs every time.

### `outputs(path, ...)`

Declares paths that must exist for a cached run to be valid. A missing output invalidates the cache even if inputs are unchanged.

```bi
outputs("target/release/app", "target/release/*.rlib")
```

---

## System Builtins

Available anywhere in the configuration file.

### Shell

| Function | Behavior |
| --- | --- |
| `sh(cmd)` | Captures stdout, throws on non-zero exit. |
| `sh(cmd, false)` | Same, but ignores the exit code. |
| `shStatus(cmd)` | Returns the exit code as an integer. |
| `shFull(cmd)` | Returns `{ code, out, err }`. |

```bi
let branch = sh("git rev-parse --abbrev-ref HEAD")

if (shStatus("which cargo") == 0) {
    run("cargo build --release")
}

let r = shFull("ls /nonexistent")
if (r.code != 0) { print("error: " + r.err) }
```

### Filesystem

| Function | Behavior |
| --- | --- |
| `exists(path)` | Boolean. |
| `glob(pattern)` | Array of matching paths. Supports `*`, `?`, `**`. |
| `which(name)` | Full path or `null`. |
| `cwd()` | Current working directory. |

```bi
let sources = glob("src/**/*.cpp")
for (src in sources) { print("found: " + src) }
```

### Environment and System

| Function | Behavior |
| --- | --- |
| `env(name)` / `env(name, default)` | Read an environment variable. |
| `os()` | `"linux"` / `"darwin"` / `"windows"`. |
| `arch()` | `"x86_64"` / `"aarch64"` / `"x86"` / `"arm"`. |
| `inCI()` | Boolean; checks `CI` / `CONTINUOUS_INTEGRATION`. |
| `pkg()` | Detects the package manager: `pnpm`, `yarn`, `bun`, `npm`, `cargo`, `uv`, `poetry`, `pip`, `go`, `composer`, `maven`, `gradle`, `bundler`, `mix`, `pub`, or `null`. |
| `time()` | Unix time in seconds. |

### Values

| Function | Behavior |
| --- | --- |
| `print(...)` | Space-separated, trailing newline. |
| `len(x)` | Length of a string (UTF-8 codepoints), array, or map. |
| `str(x)`, `int(x)`, `num(x)`, `bool(x)` | Conversions. |
| `type(x)` | Type name as a string. |
| `range(n)`, `range(a, b)`, `range(a, b, step)` | Array of integers. |
| `upper(s)`, `lower(s)`, `trim(s)` | String transforms. |

### Map, Array, and String Access

```bi
let cfg = { host: "localhost", port: 8080 }
print(cfg.host)         // "localhost"
print(cfg["port"])      // 8080
cfg.port = 9090         // assignment

let items = [10, 20, 30]
print(items[0])         // 10
print(items[-1])        // 30
items[1] = 25           // assignment

let s = "héllo"
print(s.length)         // 5
print(s[0])             // "h"
print(s[-1])            // "o"
```

---

## Caching

A task with `inputs()` is skipped when:

1. The combined content hash of all matched input files is unchanged.
2. All declared `outputs()` exist.

The hash covers more than file contents. It also includes:

- Every string passed to `run()`
- The `desc()` text
- The `depends()` list
- The `outputs()` list

Editing a command invalidates the cache even if no source changed. You will never get a stale result from a stale definition.

### Cache File

Lives at `.birun/cache`. Plain text, one entry per line, tab-separated:

```text
build	a3f8b2c91e4d7788
test	7b1e9f0a4c2d3e5f
```

Delete it at any time. `birun --clean` does it for you. To relocate:

```sh
export BIRUN_CACHE_FILE=/tmp/birun-cache
```

### Force and No-Cache

| Flag | Behavior |
| --- | --- |
| `--force` | Ignore cache hits for this run, but still refresh the cache afterward. |
| `--no-cache` | Never read from or write to the cache. |
| `--clean` | Delete the cache file. With a task name, clean then run. |

### When Caching Can Be Wrong

`birun` hashes what you tell it to. If a task reads from something you did not declare as an input — a remote service, a file outside the project, the current time — the cache will serve a stale result. Declare everything the task actually depends on. If a task genuinely depends on the wall clock, give it no inputs, and it runs every time.

---

## Parallel Execution

```sh
birun -j4 ci
birun --jobs 8 ci
birun -j4 ci        # same as --jobs 4
```

Worker threads pull ready tasks, run their commands, and mark dependents as ready. Cycles are detected at planning time.

Output is buffered per task and flushed only when the task completes. Parallel output never interleaves, even at `-j8`. Trade-off: no incremental output during a long task.

### Measured Scaling

Eight independent tasks, each sleeping one second:

| Jobs | Wall Time | Speedup | Efficiency |
| --- | --- | --- | --- |
| 1 | 8.04s | 1.00× | 100% |
| 2 | 4.04s | 1.99× | 99.5% |
| 4 | 2.02s | 3.98× | 99.5% |
| 8 | 1.03s | 7.81× | 97.6% |

Benchmark script at `examples/bench.sh`.

### Interactive Commands

Sequential mode uses `std::system` — the child inherits stdin/stdout/stderr, so interactive commands work. Parallel mode uses `popen` and captures output; stdin is not forwarded. Run with `-j1` if you need to prompt.

---

## Watch Mode

```sh
birun --watch test
```

Polls every ~400 ms. On change: waits 300 ms for the filesystem to settle, reloads the configuration, and re-runs. `Ctrl-C` stops cleanly.

### Watched Extensions

```text
.bi .sh .bash .zsh .fish .c .h .cc .cpp .hpp .cxx .hxx .rs .go .py .rb .js .jsx
.ts .tsx .java .kt .swift .cs .php .lua .ex .exs .erl .hs .ml .scala .clj .json
.yaml .yml .toml .ini .cfg .conf .env .md .rst .txt .html .css .scss .sass .less
.sql .proto .graphql
```

### Ignored Directories

```text
.git .hg .svn build .build dist node_modules .cache .next target __pycache__
.venv venv .idea .vscode
```

### Config Reload

Editing `birun.bi` while watching reloads it before the next run. If the new configuration has a syntax error, `birun` prints the error and keeps watching — the previous configuration stays active. Broken saves never kill the session.

### Debouncing

Three files written in fifty milliseconds trigger one build, not three. The window is 300 ms after the last change.

---

## CLI Reference

```text
birun <task>                 Run task and all its dependencies
birun                        List tasks (same as --list)

  -l, --list                 List available tasks
  -n, --dry-run              Show the plan; do not execute
  -j, --jobs N               Run N tasks in parallel (default: 1)
  -w, --watch                Re-run on file changes
      --no-cache             Disable caching (read + write)
      --force                Ignore cache hits; still refresh the cache
      --clean                Delete the cache file
  -q, --quiet                Errors only; no decoration
      --verbose              Print plan, cache decisions, and timing
  -f, --file PATH            Use a specific config file
  -h, --help                 Show help
  -v, --version              Print version

Integration:
      --json [task]          JSON of all tasks, or plan for one task
      --graph                Graphviz DOT of the task graph
      --completion SHELL     Print completion (bash | zsh | fish)
      --init                 Create a starter birun.bi
```

Both `-j 4` and `-j4` work. Same for `-f path` and `-fpath`.

### Examples

```sh
birun build                  # run build (and its deps)
birun -j8 ci                 # parallel CI
birun --dry-run test         # preview what would run
birun --verbose build        # show cache decisions
birun --force test           # ignore cache, then refresh it
birun --clean                # wipe cache, exit
birun --clean && birun build # wipe cache, then build fresh
birun --json ci | jq .       # plan as JSON
birun --graph | dot -Tsvg    # task graph as SVG
```

---

## Integration

### JSON Output

```sh
birun --json | jq '.tasks[].name'
birun --json test | jq '.steps[] | {name, runs}'
```

Plan object:

```json
{
  "target": "test",
  "file": "birun.bi",
  "jobs": 4,
  "count": 3,
  "steps": [
    { "name": "build", "desc": "Compile", "depends": [],        "runs": ["cargo build"] },
    { "name": "lint",  "desc": "Lint",    "depends": [],        "runs": ["cargo clippy"] },
    { "name": "test",  "desc": "Test",    "depends": ["build"], "runs": ["cargo test"] }
  ]
}
```

### Graphviz Export

```sh
birun --graph | dot -Tsvg -o tasks.svg
birun --graph | dot -Tpng -o tasks.png
```

### Shell Completion

```sh
birun --completion bash | sudo tee /etc/bash_completion.d/birun
birun --completion zsh  > ~/.zsh/completions/_birun
birun --completion fish > ~/.config/fish/completions/birun.fish
```

### CI Recipe

```yaml
# .github/workflows/build.yml
name: build

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install birun
        run: |
          curl -fsSL https://raw.githubusercontent.com/barshansarkar/birun/main/install.sh | sh
          echo "$HOME/.local/bin" >> "$GITHUB_PATH"

      - name: Cache birun state
        uses: actions/cache@v4
        with:
          path: .birun
          key: birun-${{ hashFiles('birun.bi') }}-${{ hashFiles('**/Cargo.lock') }}

      - name: Run CI
        run: birun -j4 --quiet ci
```

Content-based cache keys mean restoring a stale cache from another branch is harmless — the hash simply misses, and the task runs.

---

## Environment Variables

| Variable | Effect |
| --- | --- |
| `BIRUN_CACHE_FILE` | Path to cache file. Default: `.birun/cache` |
| `BIRUN_ASCII` | Set to `1` for ASCII symbols instead of Unicode |
| `BIRUN_INSTALL` | Install directory for `install.sh`. Default: `~/.local/bin` |
| `NO_COLOR` | Disable ANSI colors |
| `CI` | Read by `inCI()` — a truthy value returns `true` |
| `CONTINUOUS_INTEGRATION` | Fallback for `CI` |
| `COLORTERM` | Detected for truecolor output |
| `TERM` | Detected for 256-color; disables Unicode on `dumb` |

```sh
BIRUN_ASCII=1 birun build                # ASCII only
NO_COLOR=1 birun build                   # no color
BIRUN_CACHE_FILE=/tmp/c birun build      # cache elsewhere
```

---

## Comparison with Other Tools

Every tool listed is worth using. This is not "birun is better" — it is "birun is different, and here is how."

| Tool | Config | Cache | Parallel | Watch | Language | Runtime |
| --- | --- | --- | --- | --- | --- | --- |
| **birun** | `birun.bi` | content-hash | true | yes | `bi` | none |
| GNU Make | `Makefile` | timestamps | limited | no | macro | sh |
| just | `justfile` | none | limited | no | small DSL | sh |
| go-task | `Taskfile.yml` | checksum | yes | yes | YAML | none |
| mage | `magefile.go` | none | yes | no | Go | Go |
| mask | `maskfile.md` | none | no | no | Markdown | sh |
| turbo | `turbo.json` | remote | yes | yes | JSON + JS | Node |
| npm scripts | `package.json` | none | no | no | JSON | Node |

**`birun` is a good fit if you want:**

- Real logic in tasks
- Caching that survives branch switches
- Small binaries with no runtime
- A build file that reads like code

**`birun` is not a good fit if you:**

- Need Windows today (planned, not shipped)
- Want remote shared caching for a large monorepo (use turbo)
- Want the most battle-hardened option (use Make)
- Would rather not learn another small language

---

## Building from Source

Requires CMake 3.16+, a C++17 compiler (GCC 7+, Clang 6+, Apple Clang 10+), and POSIX threads.

```sh
git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Binary at `build/birun`.

### With Tests

```sh
cmake -S . -B build -DBIRUN_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Install System-Wide

```sh
sudo cmake --install build
```

Default prefix `/usr/local`. Override with `-DCMAKE_INSTALL_PREFIX=/your/prefix`.

### Cross-Compilation

```sh
cmake -S . -B build-arm64 \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j
```

---

## Architecture

```text
src/
  lexer.hpp              tokenizes bi source
  parser.hpp             builds an AST, folds constants
  ast.hpp                AST node definitions
  interpreter.hpp        tree-walking evaluator
  value.hpp              tagged union value type
  builtins.hpp           core builtins
  builtins_birun.hpp     task-runner-specific builtins
  executor.hpp/cpp       task planning and execution
  cache.hpp              content-hash caching
  watch.hpp              polling file watcher
  theme.hpp              color and symbol output
  extras.hpp             JSON, Graphviz, completion, --init
  main.cpp               CLI entry point
```

**Evaluator.** A straightforward tree walker. No bytecode, no JIT, no optimizations beyond constant folding. The parse-evaluate cycle takes under a millisecond even for a thousand-line configuration. Control flow uses a return-signal flag rather than exceptions for the hot path.

**Executor.** Builds a dependency graph, topologically sorts it, and dispatches ready tasks to worker threads. One mutex for the ready queue, one for output flushing. Task commands run without holding any lock. Sequential mode uses `std::system`; parallel mode uses `popen`.

**Cache.** One `uint64_t` FNV-1a hash per task. Stored as hex in a tab-separated text file. O(1) lookup, O(1) update, no database.

---

## Testing

```sh
cmake -S . -B build -DBIRUN_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

53 tests, 0 failures.

Coverage includes the lexer, parser, interpreter, executor, cache, and utility code:

- **Lexer:** all token types, escapes, numbers, identifiers
- **Parser:** precedence, associativity, constant folding, error recovery
- **Interpreter:** arithmetic, comparison, string ops, arrays, maps, closures, control flow
- **Executor:** dependency resolution, cycle detection, parallel scheduling
- **Cache:** hash stability, glob expansion, cache store round-trip
- **Utilities:** JSON escaping, Graphviz output, shell completion

Add a test by creating a file matching `tests/test_*.cpp`. CMake picks it up automatically.

---

## Roadmap

### v0.7.0 — Production Hardening

- `timeout(seconds)` — kill long-running tasks
- `retry(n)` and `retry(n, backoff)` — retry transient failures
- `env("KEY", "value")` and `cwd("path")` inside tasks
- `--stats` flag — durations, cache hit ratio, parallelism efficiency
- `--explain` flag — why each task ran or skipped

### v0.8.0 — Documentation and Ecosystem

- `docs/language.md` — full `bi` reference
- Homebrew tap, AUR package, Scoop manifest
- Man page
- Error messages with source snippets

### v0.9.0 — Windows Support

- Abstract process layer behind a small API
- Build and test on Windows
- Publish Windows binaries
- Handle path separators, `PATHEXT`, native shell differences

### v1.0.0 — Stability Commitment

- Freeze `bi` language
- Freeze CLI interface
- Freeze cache file format
- Semver guarantees from that point forward

### Beyond 1.0

- Remote cache protocol
- Workspace / monorepo support
- Plugin system
- Parallel-safe detection (tasks that cannot safely run together)

---

## Contributing

Contributions are welcome. The project is small enough that a focused PR can land in a day.

### Before You Start

Open an issue describing what you want to change and why. This avoids a weekend spent on a PR that does not fit the project's direction.

### Guidelines

- One feature or fix per pull request.
- Add a test in `tests/` for any new behavior.
- Ensure `ctest` passes before opening the PR.
- Match the existing style: C++17, no exceptions in hot paths, `snake_case` for locals, `CamelCase` for types.
- Keep the `bi` language frozen unless the change is specifically about extending it.

### What We're Looking For

- Bug fixes with regression tests
- Documentation improvements
- New builtins that do not require language changes
- Windows support work
- Performance improvements with benchmarks

### What We're Not Looking For

- New language features (`bi` is deliberately minimal)
- Plugin systems (contradicts the one-binary philosophy)
- Configuration formats other than `.bi`
- Features requiring a runtime dependency

### Running Tests Locally

```sh
cmake -S . -B build -DBIRUN_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -V
```

Use `-V` for individual output, `-R <pattern>` for a subset:

```sh
ctest --test-dir build -R cache
```

---

## License

MIT. See [LICENSE](LICENSE).

Use in commercial software, modify, redistribute, sell — no conditions beyond preserving the copyright notice. No CLA, no plan to change the license.

---

<div align="center">

If `birun` saves you time, please ⭐ the repository — it helps others find it.

</div>
