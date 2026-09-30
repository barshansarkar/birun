<div align="center">

# birun

**A tiny, fast task runner powered by the bi language.**

Define your build steps with real code — not brittle shell scripts or limited YAML.

[![CI](https://github.com/barshansarkar/birun/actions/workflows/ci.yml/badge.svg)](https://github.com/barshansarkar/birun/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/barshansarkar/birun)](https://github.com/barshansarkar/birun/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

---

## Why birun?

Most task runners force you into one extreme: YAML (rigid) or Make (arcane). birun gives you a real language — small, readable, powerful — with a task runner built around it.

- **Real language, not configuration.** `if`, `for`, `let`, `fn` — reuse logic across tasks.
- **Task-aware caching.** Skip tasks whose inputs haven't changed (content hash, not timestamps).
- **True parallelism.** `--jobs 8` runs 8 tasks at once. Benchmarked 7.8× speedup on 8 cores.
- **Watch mode.** Re-runs on file changes with built-in ignore rules.
- **Single static binary.** No runtime, no JVM, no Node.
- **Zero config to start.** `birun --init` scaffolds a working config.

---

## Install

### Linux / macOS — one-liner

```sh
curl -fsSL https://raw.githubusercontent.com/barshansarkar/birun/main/install.sh | sh


brew install barshansarkar/tap/birun

git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build


birun --version


cd my-project
birun --init               # creates birun.bi
birun --list               # show available tasks
birun build                # run 'build' (and its dependencies)
birun -j4 test             # 4 tasks in parallel
birun --watch test         # re-run on file changes


let mgr  = pkg()          // auto-detect: npm / cargo / pip / go / ...
let isCI = inCI()

route TASK "/build" {
    desc("Build the project")
    inputs("src/**/*.cpp", "include/**/*.hpp")
    outputs("build/app")

    if (mgr == "cargo") {
        run("cargo build --release")
    } else if (mgr == "npm" || mgr == "pnpm") {
        run("npm run build")
    } else {
        run("echo 'no build system detected'")
    }
}

route TASK "/test" {
    desc("Run the test suite")
    depends("build")
    run("ctest --output-on-failure")
}

route TASK "/ci" {
    desc("Full CI pipeline")
    depends("build", "test")
}


Task builtins

These may only be called inside a route TASK "/name" { ... } block.
Builtin	Purpose
desc("...")	Human-readable description
depends("a", "b")	Run these tasks first
run("cmd")	Add a shell command
inputs("glob", ...)	Files to hash for caching
outputs("path", ...)	Files that must exist for cache hit
System builtins
Builtin	Returns
sh("cmd")	stdout as string (throws on non-zero exit)
shStatus("cmd")	exit code only
shFull("cmd")	{ code, out, err }
exists("path")	bool
glob("src/*.bi")	array of paths
which("cargo")	full path or null
env("NAME")	string or null
os() / arch()	"linux", "darwin", "x86_64", "arm64"
inCI()	bool
pkg()	detected package manager
cwd() / time()	host info
print, len, str, int, num, range, upper, lower, trim	value helpers


birun <task>                Run task and its dependencies
birun                       List tasks

  -l, --list                List available tasks
  -n, --dry-run             Show plan; do not execute
  -j, --jobs N              Run N tasks in parallel (default: 1)
  -w, --watch               Re-run on file changes
      --no-cache            Disable caching
      --force               Ignore cache hits; still refresh cache
      --clean               Delete the cache file
  -q, --quiet               Errors only
      --verbose             Extra diagnostics
  -f, --file PATH           Use a specific config file
  -h, --help                Show help
  -v, --version             Print version

Integration:
      --json [task]         JSON output (all tasks, or plan for one)
      --graph               Graphviz DOT
      --completion SHELL    bash | zsh | fish
      --init                Create a starter birun.bi


      Caching

Tasks with inputs() are skipped when:

    The content hash of all matched input files is unchanged.

    All declared outputs() exist on disk.

The cache key also covers the task definition — its run() commands, desc(), depends(), outputs(). Editing any of them invalidates the cache.
bi

route TASK "/build" {
    inputs("src/**/*.cpp")
    outputs("build/app")
    run("cmake --build build -j")
}

sh

birun build                # runs
birun build                # "cached" — instant
birun --force build        # re-runs
birun --clean              # wipe cache

Cache file: .birun/cache. Override with BIRUN_CACHE_FILE=/path.
Parallel execution

Real benchmark (8 independent 1-second tasks):
Jobs	Wall time	Speedup
1	8.04s	1.00×
2	4.04s	1.99×
4	2.02s	3.98×
8	1.03s	7.81×

Reproduce:
sh

cd examples
./bench.sh

Watch mode
sh

birun --watch test

    Polls every ~400ms.

    Ignores .git, node_modules, target, build, dist, .venv, __pycache__.

    Only watches source-like extensions (.bi, .rs, .cpp, .py, .js, .ts, .go, .toml, .json, ...).

    On change: reloads config, re-runs task.

    Debounces rapid saves (300ms).

    Ctrl-C stops cleanly.

Integration
JSON output
sh

birun --json | jq '.tasks[].name'
birun --json test | jq '.steps[] | {name, runs}'

Graphviz
sh

birun --graph | dot -Tsvg -o tasks.svg

Shell completion
sh

birun --completion bash | sudo tee /etc/bash_completion.d/birun
birun --completion zsh  > ~/.zsh/completions/_birun
birun --completion fish > ~/.config/fish/completions/birun.fish

CI recipe
yaml

- name: Install birun
  run: curl -fsSL https://raw.githubusercontent.com/barshansarkar/birun/main/install.sh | sh

- name: Run CI
  run: ~/.local/bin/birun -j4 --quiet ci

Environment variables
Variable	Effect
BIRUN_CACHE_FILE	Path to cache file (default: .birun/cache)
BIRUN_ASCII	Set to 1 for ASCII symbols
BIRUN_INSTALL	Install directory for install.sh (default: ~/.local/bin)
NO_COLOR	Disable ANSI colors
CI	Read by inCI()
Comparison
Tool	Config	Cache	Parallel	Watch	Language
birun	birun.bi (bi)	✅ content-hash	✅ true	✅	small DSL
GNU Make	Makefile	❌	⚠️ -j	❌	none
just	justfile	❌	⚠️	❌	small DSL
go-task	Taskfile.yml	✅ checksum	✅	✅	YAML
mage	magefile.go	❌	✅	❌	Go
turbo	turbo.json	✅ remote	✅	✅	JSON + JS
npm scripts	package.json	❌	❌	❌	JSON

birun is for you if:

    You want logic in your tasks, but not a full runtime.

    You want real caching without a JS monorepo tool.

    You like small binaries and clean terminal output.

birun might not be for you if:

    You need Windows today (planned).

    You want remote cache for a large monorepo (use turbo/nx).

Building from source
sh

git clone https://github.com/barshansarkar/birun
cd birun
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

Run tests
sh

cmake -S . -B build -DBIRUN_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

Status

v0.6.0 — pre-1.0, actively developed.

    ✅ Linux x86_64 / aarch64

    ✅ macOS x86_64 / arm64

    ❌ Windows (planned)

    53 tests passing

    7.8× parallel speedup on 8 cores

License

MIT — see LICENSE.
<div align="center">

If birun saves you time, please ⭐ the repo.
</div> ```