#include "bi/interpreter.hpp"
#include "bi/value.hpp"

#include "builtins_birun.hpp"
#include "cache.hpp"
#include "executor.hpp"
#include "extras.hpp"
#include "proc.hpp"
#include "theme.hpp"
#include "watch.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace bi;
using namespace birun;
using namespace birun::theme;

#ifndef BIRUN_VERSION
#define BIRUN_VERSION "0.0.0-dev"
#endif

static const char* VERSION = BIRUN_VERSION;

static std::map<std::string, Task> g_tasks;
static std::vector<std::string>    g_taskOrder;
static Options                     g_opt;

static bool        g_jsonMode       = false;
static bool        g_graphMode      = false;
static bool        g_initMode       = false;
static bool        g_initForce      = false;
static std::string g_completionShell;

// ============================================================
//  CLI: usage
// ============================================================
static void usage() {
    std::cout
        << "  " << bold(accent("birun")) << " "
        << rule(sym::dot()) << " "
        << hint(std::string("v") + VERSION) << "\n"
        << "  " << hint("a tiny task runner powered by the bi language") << "\n\n"

        << "  " << bold(text("USAGE")) << "\n"
        << "    " << accent("birun") << " " << hint("<task>") << "          Run a task (and its dependencies)\n"
        << "    " << accent("birun") << " " << hint("-l, --list") << "      List available tasks\n"
        << "    " << accent("birun") << " " << hint("-n, --dry-run") << "   Show what would run, don't execute\n"
        << "    " << accent("birun") << " " << hint("-j, --jobs N") << "    Run N tasks in parallel (default: 1)\n"
        << "    " << accent("birun") << " " << hint("-w, --watch") << "     Re-run on file changes\n"
        << "    " << accent("birun") << " " << hint("--no-cache") << "        Disable caching\n"
        << "    " << accent("birun") << " " << hint("--force") << "           Re-run even if cached (still refreshes cache)\n"
        << "    " << accent("birun") << " " << hint("--clean") << "           Delete the cache file (then run, if a task is given)\n"
        << "    " << accent("birun") << " " << hint("-q, --quiet") << "       Suppress decoration, errors only\n"
        << "    " << accent("birun") << " " << hint("-qq, --silent") << "     Absolute silence (exit code only)\n"
        << "    " << accent("birun") << " " << hint("--verbose") << "         Extra diagnostics (plan, cache, timing)\n"
        << "    " << accent("birun") << " " << hint("--stats") << "           Per-task timings + cache hit ratio\n"
        << "    " << accent("birun") << " " << hint("--explain") << "         Explain why each task ran\n"
        << "    " << accent("birun") << " " << hint("--grace N") << "         SIGTERM→SIGKILL grace seconds (default: 3)\n"
        << "    " << accent("birun") << " " << hint("-f, --file <p>") << "  Use a specific config file\n"
        << "    " << accent("birun") << " " << hint("-h, --help") << "      Show this help\n"
        << "    " << accent("birun") << " " << hint("-v, --version") << "   Print version\n\n"

        << "  " << bold(text("INTEGRATION")) << "\n"
        << "    " << accent("birun") << " " << hint("--json") << "          All tasks as JSON\n"
        << "    " << accent("birun") << " " << hint("--json <task>") << "   Execution plan for <task> as JSON\n"
        << "    " << accent("birun") << " " << hint("--graph") << "         Print task graph as Graphviz DOT\n"
        << "    " << accent("birun") << " " << hint("--completion SH") << " Shell completion (bash|zsh|fish)\n"
        << "    " << accent("birun") << " " << hint("--init") << "          Create a starter birun.bi\n\n"

        << "  " << bold(text("TASK BUILTINS")) << "\n"
        << "    " << info("desc") << "(\"...\")            Set description\n"
        << "    " << info("depends") << "(\"a\", \"b\")      Declare dependencies\n"
        << "    " << info("run") << "(\"cmd\")           Add a shell command\n"
        << "    " << info("inputs") << "(\"src/*.cpp\")     Source globs (for caching)\n"
        << "    " << info("outputs") << "(\"build/app\")     Expected outputs (for caching)\n"
        << "    " << info("timeout") << "(sec)             Kill task's commands after N seconds\n"
        << "    " << info("retry") << "(n, backoffMs)      Retry the whole task up to n+1 times\n"
        << "    " << info("env") << "(\"K\", \"V\")         Per-task environment variable\n"
        << "    " << info("cwd") << "(\"path\")           Per-task working directory\n\n"

        << "  " << bold(text("SYSTEM BUILTINS")) << "\n"
        << "    " << info("sh") << "(\"cmd\")              Run command, capture stdout\n"
        << "    " << info("shStatus") << "(\"cmd\")          Exit code only\n"
        << "    " << info("shFull") << "(\"cmd\")            {code, out, err, timedOut}\n"
        << "    " << info("which") << "(\"tool\")          Full path or null\n"
        << "    " << info("exists") << "(\"path\")          bool\n"
        << "    " << info("glob") << "(\"src/*.bi\")       List of paths\n"
        << "    " << info("pkg") << "()                   Detect package manager\n"
        << "    " << info("cwd") << "() / " << info("os") << "() / " << info("arch") << "()   Host info\n"
        << "    " << info("inCI") << "() / " << info("env") << "(\"X\")      Environment\n\n"

        << "  " << bold(text("CACHING")) << "\n"
        << "    Tasks with " << info("inputs") << "() are skipped when their source files\n"
        << "    are unchanged AND their " << info("outputs") << "() exist.\n"
        << "    Cache key = commands + desc + depends + outputs + env + cwd\n"
        << "                 + timeout + retry + input content hash.\n"
        << "    Cache file: " << hint(".birun/cache") << "  (override: " << hint("BIRUN_CACHE_FILE") << ")\n"
        << "    Concurrent runs are safe — writes are flock()-protected.\n\n"

        << "  " << bold(text("ENVIRONMENT")) << "\n"
        << "    " << hint("BIRUN_ASCII=1") << "       Use ASCII symbols\n"
        << "    " << hint("NO_COLOR=1") << "          Disable colors\n"
        << "    " << hint("BIRUN_CACHE_FILE") << "    Path to cache file\n";
}

// ============================================================
//  File I/O
// ============================================================
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ============================================================
//  CLI: --jobs parser
// ============================================================
static int parseJobs(const std::string& s) {
    try {
        int n = std::stoi(s);
        if (n < 1) throw std::runtime_error("must be >= 1");
        if (n > 64) n = 64;
        return n;
    } catch (std::exception& e) {
        throw std::runtime_error("invalid --jobs value '" + s + "': " + e.what());
    }
}

// ============================================================
//  Config loader
// ============================================================
static void loadConfig(const std::string& path) {
    g_tasks.clear();
    g_taskOrder.clear();

    std::string src = readFile(path);
    if (src.empty())
        throw std::runtime_error("cannot read '" + path + "'");

    Interpreter interp;
    registerBirunBuiltins(interp.globals());
    interp.runSource(src, path);

    for (const auto& fn : interp.routes()) {
        if (fn->routeMethod != "TASK")
            throw std::runtime_error(
                "unknown route method '" + fn->routeMethod +
                "' — use `route TASK \"/name\" { ... }`");

        std::string tname = fn->routePath;
        if (!tname.empty() && tname[0] == '/') tname = tname.substr(1);
        if (tname.empty())
            throw std::runtime_error("task name cannot be empty");
        if (g_tasks.count(tname))
            throw std::runtime_error("duplicate task '" + tname + "'");

        Task t;
        t.name = tname;

        Task* prev = g_currentTask;
        g_currentTask = &t;
        try {
            ValueList noargs;
            interp.call(vfunc(fn), noargs);
        } catch (...) {
            g_currentTask = prev;
            throw;
        }
        g_currentTask = prev;

        g_tasks.emplace(tname, std::move(t));
        g_taskOrder.push_back(tname);
    }
}

// ============================================================
//  Print task list
// ============================================================
static void printList() {
    if (g_opt.silent) return;
    if (g_taskOrder.empty()) {
        std::cout << "  " << warn(sym::idle()) << " "
                  << hint("no tasks in " + g_opt.file) << "\n";
        return;
    }

    std::cout << "  " << bold(accent("birun")) << " "
              << rule(sym::dot()) << " "
              << text(g_opt.file) << "\n\n";

    for (const auto& taskName : g_taskOrder) {
        const Task& t = g_tasks.at(taskName);
        std::cout << "  " << hint(sym::idle()) << " "
                  << name(padName(taskName, 22));
        if (!t.desc.empty()) std::cout << text(t.desc);
        if (!t.depends.empty()) {
            std::cout << "  " << rule(sym::arrow()) << " ";
            for (size_t i = 0; i < t.depends.size(); i++) {
                if (i) std::cout << rule(", ");
                std::cout << hint(t.depends[i]);
            }
        }
        if (!t.inputs.empty()) {
            std::cout << "  " << rule("[cache]");
        }
        if (t.timeoutSec > 0)
            std::cout << "  " << rule("[timeout " +
                                      std::to_string(t.timeoutSec) + "s]");
        if (t.retryCount > 0)
            std::cout << "  " << rule("[retry " +
                                      std::to_string(t.retryCount) + "]");
        std::cout << "\n";
    }
}

// ============================================================
//  Run once
// ============================================================
static int runOnce() {
    Executor ex(g_tasks, g_opt);
    return ex.run(g_opt.task);
}

// ============================================================
//  Watch mode
// ============================================================
static int runWatchMode() {
    if (g_opt.silent) return 0;   // silent watch makes no sense
    const std::string& cfgFile = g_opt.file;
    std::vector<std::string> roots = { cfgFile, "." };

    std::cout << "  " << bold(accent("birun")) << " "
              << rule(sym::dot()) << " "
              << text("watch") << " "
              << hint(cfgFile) << " "
              << rule(sym::dot()) << " "
              << hint("Ctrl-C to stop") << "\n\n";

    auto lastSnap = birun::watch::snapshot(roots);
    try {
        runOnce();
    } catch (std::exception& e) {
        std::cerr << "  " << danger(sym::fail()) << " "
                  << danger(e.what()) << "\n";
    }

    for (;;) {
        if (proc::shutdownRequested()) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        if (proc::shutdownRequested()) break;

        auto nowSnap = birun::watch::snapshot(roots);
        auto changed = birun::watch::diff(lastSnap, nowSnap);
        if (changed.empty()) continue;

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (proc::shutdownRequested()) break;

        auto settled = birun::watch::snapshot(roots);
        changed = birun::watch::diff(lastSnap, settled);
        lastSnap = std::move(settled);

        std::cout << "\n  " << warn(sym::spark()) << " "
                  << warn("change detected") << " "
                  << rule(sym::dot()) << " "
                  << hint(std::to_string(changed.size()) + " file(s)") << "\n";
        for (size_t i = 0; i < changed.size() && i < 5; i++)
            std::cout << "      " << rule(sym::bullet()) << " "
                      << hint(changed[i]) << "\n";
        if (changed.size() > 5)
            std::cout << "      " << rule(sym::bullet()) << " "
                      << hint("... and " +
                              std::to_string(changed.size() - 5) + " more")
                      << "\n";
        std::cout << "\n";

        try {
            loadConfig(g_opt.file);
        } catch (std::exception& e) {
            std::cerr << "  " << danger(sym::fail()) << " "
                      << danger(std::string("config error: ") + e.what())
                      << "\n";
            continue;
        }

        try {
            runOnce();
        } catch (std::exception& e) {
            std::cerr << "  " << danger(sym::fail()) << " "
                      << danger(e.what()) << "\n";
        }
    }

    return 0;
}

// ============================================================
//  main
// ============================================================
int main(int argc, char** argv) {
    // Install signal handlers FIRST — so any child spawned later is
    // tracked and killed cleanly on SIGINT / SIGTERM / SIGHUP.
    proc::installSignalHandlers();

    try {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];

            // ---- combined short forms: -jN, -fPATH ----
            if (a.size() > 2 && a[0] == '-' && a[1] == 'j') {
                g_opt.jobs = parseJobs(a.substr(2));
                continue;
            }
            if (a.size() > 2 && a[0] == '-' && a[1] == 'f') {
                g_opt.file = a.substr(2);
                continue;
            }
            // ---- long form with '=': --jobs=N, --file=PATH, --grace=N ----
            if (a.rfind("--jobs=", 0) == 0) {
                g_opt.jobs = parseJobs(a.substr(7));
                continue;
            }
            if (a.rfind("--file=", 0) == 0) {
                g_opt.file = a.substr(7);
                continue;
            }
            if (a.rfind("--grace=", 0) == 0) {
                int s = std::stoi(a.substr(8));
                if (s < 0) s = 0;
                if (s > 60) s = 60;
                g_opt.graceSec = s;
                continue;
            }

            if (a == "--help" || a == "-h")    { usage(); return 0; }
            if (a == "--version" || a == "-v") {
                std::cout << "birun " << VERSION << "\n";
                return 0;
            }
            if (a == "--list" || a == "-l")    { g_opt.list = true; continue; }
            if (a == "--dry-run" || a == "-n") { g_opt.dryRun = true; continue; }
            if (a == "--watch" || a == "-w")   { g_opt.watch = true; continue; }
            if (a == "--no-cache")             { g_opt.noCache = true; continue; }
            if (a == "--force" || a == "-F")   { g_opt.force = true; continue; }
            if (a == "--clean")                { g_opt.clean = true; continue; }
            if (a == "--json")                 { g_jsonMode = true; continue; }
            if (a == "--graph")                { g_graphMode = true; continue; }
            if (a == "--init")                 { g_initMode = true; continue; }
            if (a == "--force-init")           { g_initForce = true; continue; }
            if (a == "--stats")                { g_opt.stats = true; continue; }
            if (a == "--explain")              { g_opt.explain = true; continue; }

            if (a == "--quiet" || a == "-q") {
                g_opt.verbosity = Verbosity::Quiet;
                continue;
            }
            if (a == "-qq" || a == "--silent") {
                g_opt.silent = true;
                g_opt.verbosity = Verbosity::Quiet;
                continue;
            }
            if (a == "--verbose") {
                g_opt.verbosity = Verbosity::Verbose;
                continue;
            }

            if (a == "--completion") {
                if (i + 1 >= argc)
                    throw std::runtime_error(
                        "--completion requires a shell (bash|zsh|fish)");
                g_completionShell = argv[++i];
                continue;
            }
            if (a == "--jobs" || a == "-j") {
                if (i + 1 >= argc)
                    throw std::runtime_error("--jobs requires a number");
                g_opt.jobs = parseJobs(argv[++i]);
                continue;
            }
            if (a == "--file" || a == "-f") {
                if (i + 1 >= argc)
                    throw std::runtime_error("--file requires a path");
                g_opt.file = argv[++i];
                continue;
            }
            if (a == "--grace") {
                if (i + 1 >= argc)
                    throw std::runtime_error("--grace requires a number");
                int s = std::stoi(argv[++i]);
                if (s < 0) s = 0;
                if (s > 60) s = 60;
                g_opt.graceSec = s;
                continue;
            }
            if (!a.empty() && a[0] == '-')
                throw std::runtime_error("unknown option '" + a + "'");

            if (g_opt.task.empty()) g_opt.task = a;
            else throw std::runtime_error("too many arguments");
        }

        birun::theme::detect();

        if (g_initMode) {
            bool ok = birun::extras::writeStarter(g_opt.file, g_initForce);
            return ok ? 0 : 1;
        }

        if (g_opt.clean) {
            bool removed = birun::cache::cleanCache();
            if (!g_opt.silent && g_opt.verbosity != Verbosity::Quiet) {
                if (removed) {
                    std::cout << "  " << success(sym::ok()) << " "
                              << text("cache cleared") << " "
                              << rule(sym::dot()) << " "
                              << hint(birun::cache::cacheFilePath()) << "\n";
                } else {
                    std::cout << "  " << hint(sym::idle()) << " "
                              << hint("no cache to clear") << " "
                              << rule(sym::dot()) << " "
                              << hint(birun::cache::cacheFilePath()) << "\n";
                }
            }
            if (g_opt.task.empty()) return 0;
        }

        loadConfig(g_opt.file);

        if (g_jsonMode) {
            if (!g_opt.task.empty()) {
                Executor ex(g_tasks, g_opt);
                auto order = ex.plan(g_opt.task);
                birun::extras::printPlanJson(
                    g_tasks, order, g_opt.task, g_opt.file, g_opt.jobs);
            } else {
                birun::extras::printJson(
                    g_tasks, g_taskOrder, g_opt.file, VERSION);
            }
            return 0;
        }

        if (g_graphMode) {
            birun::extras::printGraph(g_tasks, g_taskOrder, g_opt.file);
            return 0;
        }
        if (!g_completionShell.empty()) {
            birun::extras::printCompletion(g_completionShell, g_taskOrder);
            return 0;
        }

        if (g_opt.watch && g_opt.task.empty())
            throw std::runtime_error(
                "--watch requires a task name, e.g. `birun --watch test`");

        if (g_opt.list || (!g_opt.watch && g_opt.task.empty())) {
            printList();
            if (!g_opt.silent && !g_opt.watch && g_opt.task.empty() && !g_opt.list)
                std::cout << "\n  "
                          << hint("run 'birun <task>' to execute a task") << "\n";
            return 0;
        }

        if (g_opt.watch)
            return runWatchMode();

        return runOnce();

    } catch (ServeSignal&) {
        if (!g_opt.silent)
            std::cerr << "  " << danger(sym::fail()) << " "
                      << danger("serve() is not allowed in birun.bi") << "\n";
        return 1;
    } catch (BiError& e) {
        if (!g_opt.silent)
            std::cerr << "  " << danger(sym::fail()) << " "
                      << danger((e.file.empty() ? g_opt.file : e.file) +
                                ":" + std::to_string(e.line) +
                                ":" + std::to_string(e.col)) << "\n"
                      << "      " << text(e.what()) << "\n";
        return 1;
    } catch (std::exception& e) {
        if (!g_opt.silent)
            std::cerr << "  " << danger(sym::fail()) << " "
                      << danger(e.what()) << "\n";
        return 1;
    }
}
// ============================================================
//  END OF FILE
// ============================================================