#include "bi/interpreter.hpp"
#include "bi/value.hpp"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace bi;

static const char* VERSION = "0.2.0";

// ============================================================
//  ANSI colors (thread-safe: just bool reads)
// ============================================================
namespace color {
    inline bool enabled = false;
    inline const char* RST() { return enabled ? "\033[0m"  : ""; }
    inline const char* BLD() { return enabled ? "\033[1m"  : ""; }
    inline const char* DIM() { return enabled ? "\033[2m"  : ""; }
    inline const char* RED() { return enabled ? "\033[31m" : ""; }
    inline const char* GRN() { return enabled ? "\033[32m" : ""; }
    inline const char* CYN() { return enabled ? "\033[36m" : ""; }
    inline const char* YEL() { return enabled ? "\033[33m" : ""; }
}

// ============================================================
//  Task model
// ============================================================
struct Task {
    std::string              name;
    std::string              desc;
    std::vector<std::string> depends;
    std::vector<std::string> runs;
};

static std::map<std::string, Task> g_tasks;
static std::vector<std::string>    g_taskOrder;

// Global CLI options
struct Options {
    bool        dryRun = false;
    int         jobs   = 1;        // 1 = sequential
    bool        list   = false;
    std::string file   = "birun.bi";
    std::string task;
    int         portOverride = 0;   // unused, kept for compat
};
static Options g_opt;

// Task being built during `route TASK "/x" { ... }`
static Task* g_current = nullptr;

// ============================================================
//  birun builtins
// ============================================================
static void registerBirunBuiltins(std::shared_ptr<Env> g) {

    bi::def(g, "desc", [](ValueList& a) -> Value {
        if (!g_current)
            throw std::runtime_error("desc() only inside `route TASK \"/...\" { ... }`");
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("desc(text): text must be a string");
        g_current->desc = std::string(a[0].strView());
        return vnil();
    });

    bi::def(g, "depends", [](ValueList& a) -> Value {
        if (!g_current)
            throw std::runtime_error("depends() only inside `route TASK \"/...\" { ... }`");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_current->depends.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error("depends: array must contain strings");
                    g_current->depends.push_back(std::string(x.strView()));
                }
            else
                throw std::runtime_error("depends: expected string or array");
        }
        return vnil();
    });

    bi::def(g, "run", [](ValueList& a) -> Value {
        if (!g_current)
            throw std::runtime_error("run() only inside `route TASK \"/...\" { ... }`");
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("run(command): command must be a string");
        g_current->runs.push_back(std::string(a[0].strView()));
        return vnil();
    });
}

// ============================================================
//  Executor (with --dry-run and --jobs N)
// ============================================================
class Executor {
public:
    // Run `target` after its dependencies, respecting g_opt.
    // Returns 0 on success, non-zero on first failure.
    int run(const std::string& target) {
        if (!g_tasks.count(target))
            throw std::runtime_error("unknown task '" + target + "'");

        // 1. Compute ordered list (topological)
        order_.clear();
        visited_.clear();
        visiting_.clear();
        visit(target);

        // 2. Dry-run: just print plan
        if (g_opt.dryRun) {
            std::cout << color::YEL() << "[dry-run]" << color::RST()
                      << " would execute " << order_.size() << " task(s):\n";
            for (const auto& n : order_) {
                const Task& t = g_tasks.at(n);
                std::cout << "  " << color::CYN() << n << color::RST();
                if (!t.desc.empty())
                    std::cout << "  " << color::DIM() << t.desc << color::RST();
                std::cout << "\n";
                for (const auto& cmd : t.runs)
                    std::cout << "      " << color::DIM() << "$ " << color::RST()
                              << cmd << "\n";
            }
            return 0;
        }

        // 3. Jobs = 1 → sequential (original behaviour)
        if (g_opt.jobs <= 1) return runSequential();

        // 4. Jobs > 1 → parallel
        return runParallel();
    }

private:
    std::vector<std::string> order_;
    std::set<std::string>    visited_;
    std::set<std::string>    visiting_;

    // ---- dependency graph traversal ----
    void visit(const std::string& name) {
        if (visited_.count(name)) return;
        if (visiting_.count(name))
            throw std::runtime_error("dependency cycle detected at task '" + name + "'");

        auto it = g_tasks.find(name);
        if (it == g_tasks.end())
            throw std::runtime_error(
                "task '" + name + "' not found (referenced as dependency)");

        visiting_.insert(name);
        for (const auto& d : it->second.depends) visit(d);
        visiting_.erase(name);
        visited_.insert(name);
        order_.push_back(name);
    }

    // ---- helper: print task header ----
    static void printHeader(const Task& t) {
        std::cout << color::BLD() << color::CYN() << "> " << t.name
                  << color::RST();
        if (!t.desc.empty())
            std::cout << "  " << color::DIM() << t.desc << color::RST();
        std::cout << "\n";
        std::cout.flush();
    }

    // ---- helper: run all commands of one task ----
    // Returns exit code (0 = ok).
    static int runCommands(const Task& t) {
        if (t.runs.empty()) {
            std::cout << color::DIM() << "  (no commands)\n" << color::RST();
            return 0;
        }
        for (const auto& cmd : t.runs) {
            std::cout << color::DIM() << "  $ " << color::RST() << cmd << "\n";
            std::cout.flush();

            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : 1;
                std::cerr << color::RED() << "x task '" << t.name
                          << "' failed (exit " << code << ")\n" << color::RST();
                return code ? code : 1;
            }
        }
        return 0;
    }

    // ---- sequential: exactly like before ----
    int runSequential() {
        for (const auto& n : order_) {
            const Task& t = g_tasks.at(n);
            printHeader(t);
            int rc = runCommands(t);
            if (rc != 0) return rc;
        }
        return 0;
    }

    // ---- parallel: run tasks with no ready deps concurrently ----
    int runParallel() {
        // Build dependency count + reverse edges
        std::map<std::string, int>              pending;   // name -> #unfinished deps
        std::map<std::string, std::vector<std::string>> rev; // dep -> [dependents]

        for (const auto& n : order_) {
            const Task& t = g_tasks.at(n);
            pending[n] = (int)t.depends.size();
            for (const auto& d : t.depends)
                rev[d].push_back(n);
        }

        std::mutex              mtx;
        std::queue<std::string> ready;
        std::atomic<int>        failed{0};
        std::atomic<int>        running{0};
        std::atomic<int>        done{0};
        int total = (int)order_.size();

        // Seed ready queue
        for (const auto& n : order_)
            if (pending[n] == 0) ready.push(n);

        // Worker loop
        std::vector<std::thread> workers;
        int nWorkers = std::min(g_opt.jobs, total);

        for (int w = 0; w < nWorkers; w++) {
            workers.emplace_back([&] {
                for (;;) {
                    std::string name;
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        if (ready.empty()) {
                            if (done.load() == total) return;
                            if (running.load() == 0) return;  // deadlock guard
                            // no work right now; yield and retry
                        }
                        if (!ready.empty()) {
                            name = ready.front();
                            ready.pop();
                        }
                    }
                    if (name.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }

                    if (failed.load() > 0) return;   // stop scheduling new work

                    running.fetch_add(1);
                    const Task& t = g_tasks.at(name);

                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        printHeader(t);
                    }
                    int rc = runCommands(t);
                    running.fetch_sub(1);
                    done.fetch_add(1);

                    if (rc != 0) {
                        failed.store(rc);
                        return;
                    }

                    std::lock_guard<std::mutex> lk(mtx);
                    for (const auto& dependent : rev[name]) {
                        if (--pending[dependent] == 0)
                            ready.push(dependent);
                    }
                }
            });
        }

        for (auto& w : workers) w.join();

        if (failed.load() != 0) return failed.load();

        // Detect unfinished tasks (shouldn't happen with cycle detection)
        if (done.load() != total) {
            std::cerr << color::RED() << "x execution stalled — "
                      << (total - done.load()) << " task(s) unfinished\n"
                      << color::RST();
            return 1;
        }
        return 0;
    }
};

// ============================================================
//  CLI
// ============================================================
static void usage() {
    std::cout <<
        "birun " << VERSION << " - a tiny task runner (bi-powered)\n"
        "\n"
        "USAGE:\n"
        "  birun <task>            Run a task (and its dependencies)\n"
        "  birun --list,  -l       List available tasks\n"
        "  birun --dry-run, -n     Show what would run, don't execute\n"
        "  birun --jobs N, -j N    Run N tasks in parallel (default: 1)\n"
        "  birun --file <path>     Use a specific config file\n"
        "  birun --help,  -h       Show this help\n"
        "  birun --version, -v     Print version\n"
        "\n"
        "CONFIG SYNTAX (birun.bi):\n"
        "  route TASK \"/name\" {\n"
        "      desc(\"Description\")\n"
        "      depends(\"other\")\n"
        "      run(\"shell command\")\n"
        "  }\n"
        "\n"
        "  Logic is allowed: let / if / else / for / env()\n";
}

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

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

int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--help" || a == "-h")    { usage(); return 0; }
            if (a == "--version" || a == "-v") {
                std::cout << "birun " << VERSION << "\n";
                return 0;
            }
            if (a == "--list" || a == "-l")    { g_opt.list = true; continue; }
            if (a == "--dry-run" || a == "-n") { g_opt.dryRun = true; continue; }
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
            if (!a.empty() && a[0] == '-')
                throw std::runtime_error("unknown option '" + a + "'");

            if (g_opt.task.empty()) g_opt.task = a;
            else throw std::runtime_error("too many arguments");
        }

        color::enabled = isatty(fileno(stdout)) != 0;

        std::string src = readFile(g_opt.file);
        if (src.empty())
            throw std::runtime_error("cannot read '" + g_opt.file + "'");

        // ---- Load config through bi ----
        Interpreter interp;
        registerBirunBuiltins(interp.globals());
        interp.runSource(src, g_opt.file);

        // ---- Convert routes -> tasks ----
        for (const auto& fn : interp.routes()) {
            if (fn->routeMethod != "TASK")
                throw std::runtime_error(
                    "unknown route method '" + fn->routeMethod +
                    "' — use `route TASK \"/name\" { ... }`");

            std::string name = fn->routePath;
            if (!name.empty() && name[0] == '/') name = name.substr(1);
            if (name.empty())
                throw std::runtime_error("task name cannot be empty");
            if (g_tasks.count(name))
                throw std::runtime_error("duplicate task '" + name + "'");

            Task t;
            t.name = name;

            Task* prev = g_current;
            g_current = &t;
            try {
                ValueList noargs;
                interp.call(vfunc(fn), noargs);
            } catch (...) {
                g_current = prev;
                throw;
            }
            g_current = prev;

            g_tasks.emplace(name, std::move(t));
            g_taskOrder.push_back(name);
        }

        // ---- List / run ----
        if (g_opt.list || g_opt.task.empty()) {
            if (g_taskOrder.empty()) {
                std::cout << "birun: no tasks in " << g_opt.file << "\n";
                return 0;
            }
            std::cout << color::BLD() << "Tasks in " << g_opt.file << ":\n"
                      << color::RST();
            for (const auto& name : g_taskOrder) {
                const Task& t = g_tasks.at(name);
                std::cout << "  " << color::CYN() << name << color::RST();
                size_t pad = (name.size() < 20) ? 20 - name.size() : 1;
                std::cout << std::string(pad, ' ');
                if (!t.desc.empty()) std::cout << t.desc;
                if (!t.depends.empty()) {
                    std::cout << " " << color::DIM() << "(depends:";
                    for (const auto& d : t.depends) std::cout << " " << d;
                    std::cout << ")" << color::RST();
                }
                std::cout << "\n";
            }
            if (g_opt.task.empty() && !g_opt.list)
                std::cout << "\nRun 'birun <task>' to execute a task.\n";
            return 0;
        }

        Executor ex;
        return ex.run(g_opt.task);

    } catch (ServeSignal&) {
        std::cerr << "birun: serve() is not allowed in birun.bi\n";
        return 1;
    } catch (BiError& e) {
        std::cerr << "birun: " << (e.file.empty() ? g_opt.file : e.file)
                  << ":" << e.line << ":" << e.col << ": " << e.what() << "\n";
        return 1;
    } catch (std::exception& e) {
        std::cerr << color::RED() << "birun: " << e.what() << color::RST() << "\n";
        return 1;
    }
}
