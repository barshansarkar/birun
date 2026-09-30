#include "executor.hpp"
#include "cache.hpp"
#include "proc.hpp"
#include "theme.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>

namespace birun {

using namespace birun::theme;

static std::mutex g_outMtx;

// ============================================================
//  ctor
// ============================================================
Executor::Executor(const std::map<std::string, Task>& tasks, const Options& opt)
    : tasks_(tasks), opt_(opt) {}

// ============================================================
//  Topological plan
// ============================================================
void Executor::visit(const std::string& name) {
    if (visited_.count(name)) return;
    if (visiting_.count(name))
        throw std::runtime_error("dependency cycle detected at task '" + name + "'");

    auto it = tasks_.find(name);
    if (it == tasks_.end())
        throw std::runtime_error(
            "task '" + name + "' not found (referenced as dependency)");

    visiting_.insert(name);
    for (const auto& d : it->second.depends) visit(d);
    visiting_.erase(name);
    visited_.insert(name);
    order_.push_back(name);
}

std::vector<std::string> Executor::plan(const std::string& target) {
    if (!tasks_.count(target))
        throw std::runtime_error("unknown task '" + target + "'");
    order_.clear();
    visited_.clear();
    visiting_.clear();
    visit(target);
    return order_;
}

// ============================================================
//  Print helpers
// ============================================================
void Executor::printHeader(const Task& t, int idx, int total) const {
    if (opt_.verbosity == Verbosity::Quiet) return;

    std::string counter;
    if (total > 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "[%d/%d]", idx, total);
        counter = rule(buf) + " ";
    }
    std::cout << "  " << counter
              << running(sym::running()) << " "
              << name(padName(t.name, 22));
    if (!t.desc.empty())
        std::cout << "  " << hint(t.desc);
    std::cout << "\n";
    std::cout.flush();
}

void Executor::printDone(const Task& t, double secs) const {
    if (opt_.verbosity == Verbosity::Quiet) return;
    std::cout << "  " << success(sym::ok()) << " "
              << name(padName(t.name, 22))
              << "  " << duration(secs) << "\n";
    std::cout.flush();
}

void Executor::printCached(const Task& t) const {
    if (opt_.verbosity == Verbosity::Quiet) return;
    std::cout << "  " << hint(sym::ok()) << " "
              << name(padName(t.name, 22))
              << "  " << hint("cached") << "\n";
    std::cout.flush();
}

void Executor::printFail(const Task& t, int code) const {
    std::cout << "  " << danger(sym::fail()) << " "
              << name(padName(t.name, 22))
              << "  " << danger("exit " + std::to_string(code))
              << "\n";
    std::cout.flush();
}

// ============================================================
//  runCommands
//    captured == nullptr  → stream live to stdout (sequential)
//    captured != nullptr  → buffer stdout+stderr (parallel)
//  Uses proc::* (fork/exec, process groups, signal-safe).
// ============================================================
int Executor::runCommands(const Task& t, std::string* captured) const {
    if (t.runs.empty()) return 0;
    const bool showCmd = (opt_.verbosity != Verbosity::Quiet);

    for (const auto& cmd : t.runs) {
        if (proc::shutdownRequested()) return 130;

        if (captured) {
            if (showCmd) {
                *captured += "    ";
                *captured += sym::arrow();
                *captured += " ";
                *captured += cmd;
                *captured += "\n";
            }
        } else if (showCmd) {
            std::cout << "    " << rule(sym::arrow()) << " "
                      << hint(cmd) << "\n";
            std::cout.flush();
        }

        int code = captured
            ? proc::runCaptured(cmd, captured)
            : proc::runLive(cmd);

        if (proc::shutdownRequested()) return 130;
        if (code != 0) return code;
    }
    return 0;
}

// ============================================================
//  Entry point
// ============================================================
int Executor::run(const std::string& target) {
    plan(target);

    if (opt_.verbosity == Verbosity::Verbose) {
        std::cout << "  " << special(sym::spark()) << " "
                  << text("plan") << " "
                  << rule(sym::dot()) << " "
                  << text(std::to_string(order_.size()) + " task(s)") << " "
                  << rule(sym::dot()) << " "
                  << text("jobs=" + std::to_string(opt_.jobs)) << " "
                  << rule(sym::dot()) << " "
                  << text("cache=" + std::string(
                        opt_.noCache ? "off" :
                        opt_.force   ? "force" : "on")) << "\n";
        for (const auto& n : order_)
            std::cout << "      " << rule(sym::bullet()) << " "
                      << name(n) << "\n";
        std::cout << "\n";
    }

    if (opt_.dryRun) {
        if (opt_.verbosity != Verbosity::Quiet) {
            std::cout << "  " << warn(sym::spark()) << " "
                      << warn("dry-run") << " "
                      << rule(sym::dot()) << " "
                      << text("would execute " + std::to_string(order_.size()) +
                              " task(s)") << "\n\n";
        }
        int idx = 0;
        for (const auto& n : order_) {
            const Task& t = tasks_.at(n);
            char buf[16];
            std::snprintf(buf, sizeof buf, "%2d", ++idx);
            std::cout << "  " << rule(buf) << "  "
                      << hint(sym::idle()) << " "
                      << name(padName(n, 22));
            if (!t.desc.empty()) std::cout << "  " << hint(t.desc);
            std::cout << "\n";
            if (opt_.verbosity == Verbosity::Verbose) {
                for (const auto& cmd : t.runs)
                    std::cout << "        " << rule(sym::arrow()) << " "
                              << hint(cmd) << "\n";
            }
        }
        return 0;
    }

    if (opt_.jobs <= 1) return runSequential();
    return runParallel();
}

// ============================================================
//  Sequential
// ============================================================
int Executor::runSequential() {
    const int total = (int)order_.size();
    int idx = 0;
    auto wall0 = std::chrono::steady_clock::now();

    std::string cachePath = cache::cacheFilePath();
    cache::CacheStore store = opt_.noCache ? cache::CacheStore{}
                                           : cache::CacheStore::load(cachePath);
    bool dirty = false;

    for (const auto& n : order_) {
        if (proc::shutdownRequested()) {
            if (dirty && !opt_.noCache) {
                try { store.save(cachePath); } catch (...) {}
            }
            return 130;
        }

        const Task& t = tasks_.at(n);

        if (!opt_.noCache && !t.inputs.empty()) {
            std::string h = cache::hashTask(t);
            auto it = store.entries.find(n);
            if (!opt_.force &&
                it != store.entries.end() && it->second == h &&
                cache::outputsExist(t.outputs)) {
                if (opt_.verbosity == Verbosity::Verbose)
                    std::cout << "  " << hint(sym::ok()) << " "
                              << name(padName(n, 22))
                              << "  " << hint("cache hit " + h.substr(0, 12))
                              << "\n";
                printCached(t);
                continue;
            }
            if (opt_.verbosity == Verbosity::Verbose)
                std::cout << "  " << warn(sym::spark()) << " "
                          << name(padName(n, 22))
                          << "  " << warn(std::string("cache miss") +
                                          (opt_.force ? " (--force)" :
                                           it == store.entries.end() ? " (new)" :
                                           " (hash changed)")) << "\n";
        }

        printHeader(t, ++idx, total);

        auto t0 = std::chrono::steady_clock::now();
        int rc = runCommands(t);
        double dt = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();

        if (rc != 0) {
            printFail(t, rc);
            if (dirty && !opt_.noCache) {
                try { store.save(cachePath); } catch (...) {}
            }
            return rc;
        }
        printDone(t, dt);

        if (!opt_.noCache && !t.inputs.empty()) {
            store.entries[n] = cache::hashTask(t);
            dirty = true;
        }
    }

    if (dirty && !opt_.noCache) {
        try { store.save(cachePath); }
        catch (std::exception& e) {
            if (opt_.verbosity != Verbosity::Quiet)
                std::cerr << "  " << warn(sym::spark()) << " "
                          << warn(std::string("cache: ") + e.what()) << "\n";
        }
    }

    if (opt_.verbosity == Verbosity::Verbose) {
        double w = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - wall0).count();
        std::cout << "\n  " << special(sym::clock()) << " "
                  << text("total") << " " << duration(w) << "\n";
    }
    return 0;
}

// ============================================================
//  Parallel
// ============================================================
int Executor::runParallel() {
    std::map<std::string, int>                      pending;
    std::map<std::string, std::vector<std::string>> rev;

    for (const auto& n : order_) {
        const Task& t = tasks_.at(n);
        pending[n] = (int)t.depends.size();
        for (const auto& d : t.depends)
            rev[d].push_back(n);
    }

    std::mutex              mtx;
    std::queue<std::string> ready;
    std::atomic<int>        failed{0};
    std::atomic<int>        runningCount{0};
    std::atomic<int>        done{0};
    int total = (int)order_.size();

    for (const auto& n : order_)
        if (pending[n] == 0) ready.push(n);

    std::vector<std::thread> workers;
    int nWorkers = std::min(opt_.jobs, total);
    if (nWorkers < 1) nWorkers = 1;

    std::string cachePath = cache::cacheFilePath();
    cache::CacheStore store = opt_.noCache ? cache::CacheStore{}
                                           : cache::CacheStore::load(cachePath);
    std::mutex cacheMtx;
    bool dirty = false;
    auto wall0 = std::chrono::steady_clock::now();

    for (int w = 0; w < nWorkers; w++) {
        workers.emplace_back([&] {
            for (;;) {
                if (failed.load() > 0) return;
                if (proc::shutdownRequested()) return;

                std::string taskName;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (ready.empty()) {
                        if (done.load() == total)         return;
                        if (runningCount.load() == 0)     return;
                    }
                    if (!ready.empty()) {
                        taskName = ready.front();
                        ready.pop();
                    }
                }
                if (taskName.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                runningCount.fetch_add(1);
                const Task& t = tasks_.at(taskName);

                // ---------- cache check ----------
                if (!opt_.noCache && !t.inputs.empty()) {
                    std::string h = cache::hashTask(t);
                    bool hit = false;
                    {
                        std::lock_guard<std::mutex> lk(cacheMtx);
                        auto it = store.entries.find(taskName);
                        if (!opt_.force &&
                            it != store.entries.end() && it->second == h &&
                            cache::outputsExist(t.outputs))
                            hit = true;
                    }
                    if (hit) {
                        {
                            std::lock_guard<std::mutex> lk(g_outMtx);
                            if (opt_.verbosity == Verbosity::Verbose)
                                std::cout << "  " << hint(sym::ok()) << " "
                                          << name(padName(taskName, 22))
                                          << "  " << hint("cache hit " + h.substr(0, 12))
                                          << "\n";
                            printCached(t);
                        }
                        runningCount.fetch_sub(1);
                        done.fetch_add(1);
                        std::lock_guard<std::mutex> lk(mtx);
                        for (const auto& dep : rev[taskName])
                            if (--pending[dep] == 0) ready.push(dep);
                        continue;
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(g_outMtx);
                    printHeader(t);
                }

                auto t0 = std::chrono::steady_clock::now();
                std::string captured;
                int rc = runCommands(t, &captured);
                double dt = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t0).count();

                runningCount.fetch_sub(1);
                done.fetch_add(1);

                {
                    std::lock_guard<std::mutex> lk(g_outMtx);
                    if (!captured.empty()) {
                        std::cout << captured;
                        if (captured.back() != '\n') std::cout << "\n";
                    }
                    if (rc == 0) printDone(t, dt);
                    else         printFail(t, rc);
                }

                if (rc != 0) {
                    failed.store(rc);
                    return;
                }

                if (!opt_.noCache && !t.inputs.empty()) {
                    std::lock_guard<std::mutex> lk(cacheMtx);
                    store.entries[taskName] = cache::hashTask(t);
                    dirty = true;
                }

                std::lock_guard<std::mutex> lk(mtx);
                for (const auto& dep : rev[taskName]) {
                    if (--pending[dep] == 0)
                        ready.push(dep);
                }
            }
        });
    }

    for (auto& w : workers) w.join();

    if (proc::shutdownRequested()) {
        if (dirty && !opt_.noCache) {
            try { store.save(cachePath); } catch (...) {}
        }
        return 130;
    }

    if (failed.load() != 0) {
        if (dirty && !opt_.noCache) {
            try { store.save(cachePath); } catch (...) {}
        }
        return failed.load();
    }

    if (done.load() != total) {
        std::cerr << "  " << danger(sym::fail()) << " "
                  << danger("execution stalled — " +
                            std::to_string(total - done.load()) +
                            " task(s) unfinished") << "\n";
        return 1;
    }

    if (dirty && !opt_.noCache) {
        try { store.save(cachePath); }
        catch (std::exception& e) {
            if (opt_.verbosity != Verbosity::Quiet)
                std::cerr << "  " << warn(sym::spark()) << " "
                          << warn(std::string("cache: ") + e.what()) << "\n";
        }
    }

    if (opt_.verbosity == Verbosity::Verbose) {
        double w = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - wall0).count();
        std::cout << "\n  " << special(sym::clock()) << " "
                  << text("total") << " " << duration(w) << "\n";
    }
    return 0;
}

} // namespace birun
// ============================================================
//  END OF FILE
// ============================================================