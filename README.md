# birun

A tiny, fast, bi-powered task runner. Config in `.bi` syntax with **real logic** (if/for/let/env).

![version](https://img.shields.io/badge/version-0.2.0-blue)
![binary](https://img.shields.io/badge/size-253KB-green)
![license](https://img.shields.io/badge/license-MIT-blue)

## Why birun?

- **Tiny** — ~253 KB binary, zero external deps
- **Fast** — parallel execution (`--jobs N`) with dependency-aware scheduling
- **Real logic** — `if`, `for`, `let`, `env()` in your config (not just YAML)
- **Safe** — cycle detection, dry-run, clear error messages with `line:col`

## Install

### From source

    git clone https://github.com/barshansarkar/birun
    cd birun
    ./build.sh
    sudo cp build/birun /usr/local/bin/

### Pre-built binary

Download from Releases:

    tar -xzf birun-linux-x86_64.tar.gz
    sudo mv birun-linux-x86_64 /usr/local/bin/birun
    birun --help

## Usage

    birun <task>            # run a task (and its dependencies)
    birun --list,  -l       # list tasks
    birun --dry-run, -n     # show what would run
    birun --jobs N,  -j N   # run N tasks in parallel
    birun --file <path>     # custom config file
    birun --help,  -h
    birun --version, -v

## Config — birun.bi

    let MODE = env("MODE", "release")

    route TASK "/build" {
        desc("Build the app (" + MODE + ")")
        if (MODE == "debug") {
            run("g++ -g -O0 -o app src/*.cpp")
        } else {
            run("g++ -O2 -o app src/*.cpp")
        }
    }

    route TASK "/test" {
        desc("Run tests")
        depends("build")
        run("./app --test")
    }

    route TASK "/clean" {
        desc("Cleanup")
        for dir in ["build", "dist"] {
            run("rm -rf " + dir)
        }
    }

    route TASK "/all" {
        desc("Everything")
        depends("build")
        depends("test")
    }

## Run

    birun build              # release build
    MODE=debug birun build   # debug build
    birun all                # build → test → all
    birun --jobs 4 all       # parallel where possible
    birun --dry-run all      # preview without executing

## Config syntax

birun uses bi's `route TASK "..." { ... }` syntax — the `TASK` method
tells birun "this is a task, not a web route".

Inside a task block:

- `desc(text)` — Set description
- `depends(name)` — Add a dependency (repeatable)
- `run(command)` — Add a shell command (repeatable)

You can use full bi syntax inside task blocks: `let`, `if`/`else`, `for`,
functions, `env()`, string concatenation — anything bi supports.

## Build from source

Requirements: C++17 compiler, CMake >= 3.16, pthreads

    ./build.sh
    # → build/birun (~253 KB)

## Roadmap

- **v0.2.0** — current: bi engine, logic (if/for/let/env), --dry-run, --jobs N
- **v0.3.0** — planned: --watch, task caching, .env files
- **v0.4.0** — planned: biconf (config with schema validation)
- **v0.5.0** — planned: multi-platform (macOS, Windows)

## License

MIT — see LICENSE