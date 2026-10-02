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

Executor::Executor(const std::map<std::string, Task>& tasks, const Options& opt)
    : tasks_(tasks), opt_(opt) {}

// ============================================================
//  plan
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
    order_.clear(); visited_.clear(); visiting_.clear();
    visit(target);
    return order_;
}

// ============================================================
//  print helpers
// ============================================================
void Executor::printHeader(const Task& t, int idx, int total) const {
    if (opt_.silent || opt_.verbosity == Verbosity::Quiet) return;
    std::string counter;
    if (total > 0) {
        char buf[32]; std::snprintf(buf, sizeof buf, "[%d/%d]", idx, total);
        counter = rule(buf) + " ";
    }
    std::cout << "  " << counter
              << running(sym::running()) << " "
              << name(padName(t.name, 22));
    if (!t.desc.empty()) std::cout << "  " << hint(t.desc);
    std::cout << "\n"; std::cout.flush();
}
void Executor::printDone(const Task& t, double secs) const {
    if (opt_.silent || opt_.verbosity == Verbosity::Quiet) return;
    std::cout << "  " << success(sym::ok()) << " "
              << name(padName(t.name, 22))
              << "  " << duration(secs) << "\n";
    std::cout.flush();
}
void Executor::printCached(const Task& t) const {
    if (opt_.silent || opt_.verbosity == Verbosity::Quiet) return;
    std::cout << "  " << hint(sym::ok()) << " "
              << name(padName(t.name, 22))
              << "  " << hint("cached") << "\n";
    std::cout.flush();
}
void Executor::printFail(const Task& t, int code) const {
    if (opt_.silent) return;
    std::cout << "  " << danger(sym::fail()) << " "
              << name(padName(t.name, 22))
              << "  " << danger("exit " + std::to_string(code)) << "\n";
    std::cout.flush();
}
void Executor::printRetry(const Task& t, int attempt, int max,
                          double waitSec) const {
    if (opt_.silent || opt_.verbosity == Verbosity::Quiet) return;
    char buf[64]; std::snprintf(buf, sizeof buf, "retry %d/%d in %.1fs",
                                attempt, max, waitSec);
    std::cout << "  " << warn(sym::spark()) << " "
              << name(padName(t.name, 22))
              << "  " << warn(buf) << "\n";
    std::cout.flush();
}
void Executor::explain(const std::string& name, const std::string& why) const {
    if (opt_.silent || !opt_.explain) return;
    std::cout << "    " << rule(sym::branch()) << " "
              << hint(name) << " " << rule(sym::dot()) << " "
              << hint(why) << "\n";
    std::cout.flush();
}

void Executor::printStats() const {
    if (opt_.silent || !opt_.stats) return;

    std::cout << "\n  " << special(sym::spark()) << " "
              << bold(text("stats")) << "\n";
    std::cout << "    " << rule(sym::dot()) << " "
              << text("total") << "   " << duration(stats_.totalSec) << "\n";
    std::cout << "    " << rule(sym::dot()) << " "
              << text("tasks") << "   "
              << text(std::to_string(stats_.tasks.size())) << "\n";
    int total = stats_.hits + stats_.misses;
    if (total > 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d/%d (%.0f%%)",
                      stats_.hits, total,
                      100.0 * stats_.hits / total);
        std::cout << "    " << rule(sym::dot()) << " "
                  << text("cached") << "  " << text(buf) << "\n";
    }
    if (!stats_.tasks.empty()) {
        std::cout << "    " << rule(sym::dot()) << " "
                  << text("slowest") << "\n";
        auto t = stats_.tasks;
        std::sort(t.begin(), t.end(),
            [](const TaskStat& a, const TaskStat& b){ return a.seconds > b.seconds; });
        for (size_t i = 0; i < t.size() && i < 5; i++) {
            std::cout << "      " << rule(sym::bullet()) << " "
                      << name(padName(t[i].name, 22))
                      << "  " << duration(t[i].seconds);
            if (t[i].cached) std::cout << "  " << hint("cached");
            if (t[i].attempts > 1)
                std::cout << "  " << warn("retries " +
                    std::to_string(t[i].attempts - 1));
            std::cout << "\n";
        }
    }
}

// ============================================================
//  runCommands
//    - honours per-task timeout / env / cwd
//    - retry wraps the WHOLE task (all runs) — see call sites
//    - NEW: checks proc::cancelRequested() between commands
// ============================================================
int Executor::runCommands(const Task& t, std::string* captured) const {
    if (t.runs.empty()) return 0;
    const bool showCmd = !opt_.silent &&
                         opt_.verbosity != Verbosity::Quiet;

    // In silent mode, force-capture & discard
    std::string discard;
    std::string* cap = captured;
    if (!cap && opt_.silent) cap = &discard;

    for (const auto& cmd : t.runs) {
        if (proc::shutdownRequested()) return 130;
        if (proc::cancelRequested())   return 130;

        if (cap) {
            if (showCmd) { *cap += "    "; *cap += sym::arrow(); *cap += " "; *cap += cmd; *cap += "\n"; }
        } else if (showCmd) {
            std::cout << "    " << rule(sym::arrow()) << " "
                      << hint(cmd) << "\n"; std::cout.flush();
        }

        proc::RunOpts ro;
        ro.timeoutMs = t.timeoutSec > 0 ? t.timeoutSec * 1000 : 0;
        ro.graceMs   = opt_.graceSec * 1000;
        ro.env       = t.env;
        ro.cwd       = t.cwd;

        proc::RunResult rr = cap ? proc::runCapturedEx(cmd, cap, ro)
                                 : proc::runLiveEx(cmd, ro);

        if (rr.timedOut) {
            if (!opt_.silent && opt_.verbosity == Verbosity::Verbose) {
                std::lock_guard<std::mutex> lk(g_outMtx);
                std::cout << "    " << warn(sym::spark()) << " "
                          << warn("timeout after " +
                                  std::to_string(t.timeoutSec) + "s "
                                  "(SIGTERM→SIGKILL after " +
                                  std::to_string(opt_.graceSec) + "s)")
                          << "\n";
                std::cout.flush();
            }
            return proc::kExitTimeout;
        }
        if (rr.cancelled)              return proc::kExitCancelled;
        if (proc::shutdownRequested()) return 130;
        if (proc::cancelRequested())   return proc::kExitCancelled;
        if (rr.exitCode != 0) return rr.exitCode;
    }
    return 0;
}

// ============================================================
//  cache probe helper — shared by seq/parallel
// ============================================================
namespace {
enum class CacheVerdict { MissNew, MissHash, MissOutputs, MissForce, Hit };

inline CacheVerdict probe(const Task& t, const cache::CacheStore& store,
                          const std::string& name, bool force,
                          std::string* hashOut) {
    std::string h = cache::hashTask(t);
    if (hashOut) *hashOut = h;
    if (force) return CacheVerdict::MissForce;
    auto it = store.entries.find(name);
    if (it == store.entries.end()) return CacheVerdict::MissNew;
    if (it->second != h)          return CacheVerdict::MissHash;
    if (!cache::outputsExist(t.outputs)) return CacheVerdict::MissOutputs;
    return CacheVerdict::Hit;
}
inline const char* verdictText(CacheVerdict v) {
    switch (v) {
        case CacheVerdict::MissNew:     return "new task";
        case CacheVerdict::MissHash:    return "inputs/definition changed";
        case CacheVerdict::MissOutputs: return "outputs missing";
        case CacheVerdict::MissForce:   return "forced (--force)";
        case CacheVerdict::Hit:         return "hit";
    }
    return "";
}
} // namespace

// ============================================================
//  run
// ============================================================
int Executor::run(const std::string& target) {
    // Fresh cancel state for this run (important for watch mode).
    proc::resetCancel();

    auto wall0 = std::chrono::steady_clock::now();
    plan(target);

    if (!opt_.silent && opt_.verbosity == Verbosity::Verbose) {
        std::cout << "  " << special(sym::spark()) << " "
                  << text("plan") << " " << rule(sym::dot()) << " "
                  << text(std::to_string(order_.size()) + " task(s)") << " "
                  << rule(sym::dot()) << " "
                  << text("jobs=" + std::to_string(opt_.jobs)) << " "
                  << rule(sym::dot()) << " "
                  << text(std::string("cache=") +
                          (opt_.noCache ? "off" :
                           opt_.force   ? "force" : "on")) << "\n";
        for (const auto& n : order_)
            std::cout << "      " << rule(sym::bullet()) << " "
                      << name(n) << "\n";
        std::cout << "\n";
    }

    if (opt_.dryRun) {
        if (!opt_.silent && opt_.verbosity != Verbosity::Quiet) {
            std::cout << "  " << warn(sym::spark()) << " "
                      << warn("dry-run") << " " << rule(sym::dot()) << " "
                      << text("would execute " +
                              std::to_string(order_.size()) + " task(s)")
                      << "\n\n";
        }
        int idx = 0;
        for (const auto& n : order_) {
            const Task& t = tasks_.at(n);
            char buf[16]; std::snprintf(buf, sizeof buf, "%2d", ++idx);
            std::cout << "  " << rule(buf) << "  "
                      << hint(sym::idle()) << " "
                      << name(padName(n, 22));
            if (!t.desc.empty()) std::cout << "  " << hint(t.desc);
            std::cout << "\n";
            if (opt_.verbosity == Verbosity::Verbose)
                for (const auto& cmd : t.runs)
                    std::cout << "        " << rule(sym::arrow()) << " "
                              << hint(cmd) << "\n";
        }
        return 0;
    }

    int rc = (opt_.jobs <= 1) ? runSequential() : runParallel();
    stats_.totalSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - wall0).count();
    printStats();
    return rc;
}

// ============================================================
//  Sequential
// ============================================================
int Executor::runSequential() {
    const int total = (int)order_.size();
    int idx = 0;

    std::string cachePath = cache::cacheFilePath();
    cache::CacheStore store = opt_.noCache ? cache::CacheStore{}
                                           : cache::CacheStore::load(cachePath);
    bool dirty = false;

    for (const auto& n : order_) {
        if (proc::shutdownRequested() || proc::cancelRequested()) {
            if (dirty && !opt_.noCache) {
                try { store.save(cachePath); } catch (...) {}
            }
            return 130;
        }
        const Task& t = tasks_.at(n);

        // ---------- cache check ----------
        bool fromCache = false;
        if (!opt_.noCache && !t.inputs.empty()) {
            std::string h;
            auto v = probe(t, store, n, opt_.force, &h);
            if (v == CacheVerdict::Hit) {
                if (opt_.verbosity == Verbosity::Verbose && !opt_.silent)
                    std::cout << "  " << hint(sym::ok()) << " "
                              << name(padName(n, 22))
                              << "  " << hint("cache hit " + h.substr(0,12))
                              << "\n";
                printCached(t);
                explain(n, "cache hit");
                std::lock_guard<std::mutex> lk(statsMtx_);
                stats_.hits++;
                stats_.tasks.push_back({n, 0.0, 1, 0, true});
                fromCache = true;
            } else {
                if (opt_.verbosity == Verbosity::Verbose && !opt_.silent)
                    std::cout << "  " << warn(sym::spark()) << " "
                              << name(padName(n, 22))
                              << "  " << warn(std::string("cache miss: ") +
                                              verdictText(v)) << "\n";
                explain(n, std::string("cache miss: ") + verdictText(v));
                std::lock_guard<std::mutex> lk(statsMtx_);
                stats_.misses++;
            }
        } else if (opt_.explain) {
            if (opt_.noCache) explain(n, "cache disabled (--no-cache)");
            else              explain(n, "no inputs declared — always runs");
        }
        if (fromCache) continue;

        // ---------- run with retry ----------
        printHeader(t, ++idx, total);

        auto t0 = std::chrono::steady_clock::now();
        int rc = 0;
        int attempts = 0;
        int maxAttempts = t.retryCount + 1;

        for (attempts = 1; attempts <= maxAttempts; attempts++) {
            rc = runCommands(t);
            if (rc == 0) break;
            if (proc::shutdownRequested()) break;
            if (proc::cancelRequested())   break;
            if (attempts < maxAttempts) {
                double waitSec = (t.retryBackoffMs / 1000.0) *
                                 (1 << (attempts - 1));   // exponential
                if (waitSec > 30.0) waitSec = 30.0;
                printRetry(t, attempts, maxAttempts - 1, waitSec);
                int ms = (int)(waitSec * 1000);
                while (ms > 0 &&
                       !proc::shutdownRequested() &&
                       !proc::cancelRequested()) {
                    int step = ms > 100 ? 100 : ms;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(step));
                    ms -= step;
                }
            }
        }
        double dt = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();

        {
            std::lock_guard<std::mutex> lk(statsMtx_);
            stats_.tasks.push_back({n, dt, attempts, rc, false});
        }

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
            if (!opt_.silent && opt_.verbosity != Verbosity::Quiet)
                std::cerr << "  " << warn(sym::spark()) << " "
                          << warn(std::string("cache: ") + e.what()) << "\n";
        }
    }
    return 0;
}

// ============================================================
//  Parallel
//
//  Failure handling:
//    - First failing task calls proc::requestCancel().
//    - That sends SIGTERM to every registered child process.
//    - Workers exit their loop on cancelRequested().
//    - SIGKILL escalation is handled by each child's own wait loop
//      (grace period) — see proc::shouldKill().
// ============================================================
int Executor::runParallel() {
    std::map<std::string, int>                      pending;
    std::map<std::string, std::vector<std::string>> rev;

    for (const auto& n : order_) {
        const Task& t = tasks_.at(n);
        pending[n] = (int)t.depends.size();
        for (const auto& d : t.depends) rev[d].push_back(n);
    }

    std::mutex              mtx;
    std::queue<std::string> ready;
    std::atomic<int>        failed{0};
    std::atomic<int>        runningCount{0};
    std::atomic<int>        done{0};
    int total = (int)order_.size();

    for (const auto& n : order_) if (pending[n] == 0) ready.push(n);

    std::string cachePath = cache::cacheFilePath();
    cache::CacheStore store = opt_.noCache ? cache::CacheStore{}
                                           : cache::CacheStore::load(cachePath);
    std::mutex cacheMtx;
    bool dirty = false;

    std::vector<std::thread> workers;
    int nW = std::min(opt_.jobs, std::max(total, 1));
    if (nW < 1) nW = 1;

    for (int w = 0; w < nW; w++) {
        workers.emplace_back([&] {
            for (;;) {
                // Exit conditions include cancel.
                if (failed.load() > 0)         return;
                if (proc::shutdownRequested()) return;
                if (proc::cancelRequested())   return;

                std::string taskName;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (ready.empty()) {
                        if (done.load() == total)     return;
                        if (runningCount.load() == 0) return;
                    }
                    if (!ready.empty()) { taskName = ready.front(); ready.pop(); }
                }
                if (taskName.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                runningCount.fetch_add(1);
                const Task& t = tasks_.at(taskName);

                // ---- cache ----
                bool fromCache = false;
                if (!opt_.noCache && !t.inputs.empty()) {
                    std::string h;
                    CacheVerdict v;
                    {
                        std::lock_guard<std::mutex> lk(cacheMtx);
                        v = probe(t, store, taskName, opt_.force, &h);
                    }
                    if (v == CacheVerdict::Hit) {
                        {
                            std::lock_guard<std::mutex> lk(g_outMtx);
                            if (opt_.verbosity == Verbosity::Verbose && !opt_.silent)
                                std::cout << "  " << hint(sym::ok()) << " "
                                          << name(padName(taskName, 22))
                                          << "  " << hint("cache hit " + h.substr(0,12))
                                          << "\n";
                            printCached(t);
                        }
                        explain(taskName, "cache hit");
                        {
                            std::lock_guard<std::mutex> lk(statsMtx_);
                            stats_.hits++;
                            stats_.tasks.push_back({taskName, 0.0, 1, 0, true});
                        }
                        fromCache = true;
                    } else {
                        if (opt_.verbosity == Verbosity::Verbose && !opt_.silent) {
                            std::lock_guard<std::mutex> lk(g_outMtx);
                            std::cout << "  " << warn(sym::spark()) << " "
                                      << name(padName(taskName, 22))
                                      << "  " << warn(std::string("cache miss: ") +
                                                      verdictText(v)) << "\n";
                        }
                        explain(taskName, std::string("cache miss: ") + verdictText(v));
                        std::lock_guard<std::mutex> lk(statsMtx_);
                        stats_.misses++;
                    }
                } else if (opt_.explain) {
                    explain(taskName, opt_.noCache ? "cache disabled (--no-cache)"
                                                   : "no inputs declared");
                }

                int rc = 0, attempts = 1;
                double dt = 0.0;

                if (!fromCache) {
                    {
                        std::lock_guard<std::mutex> lk(g_outMtx);
                        printHeader(t);
                    }
                    auto t0 = std::chrono::steady_clock::now();
                    std::string captured;
                    int maxAttempts = t.retryCount + 1;

                    for (attempts = 1; attempts <= maxAttempts; attempts++) {
                        captured.clear();
                        rc = runCommands(t, &captured);
                        if (rc == 0) break;
                        if (proc::shutdownRequested()) break;
                        if (proc::cancelRequested())   break;

                        if (attempts < maxAttempts) {
                            double waitSec = (t.retryBackoffMs / 1000.0) *
                                             (1 << (attempts - 1));
                            if (waitSec > 30.0) waitSec = 30.0;
                            {
                                std::lock_guard<std::mutex> lk(g_outMtx);
                                printRetry(t, attempts, maxAttempts - 1, waitSec);
                            }
                            int ms = (int)(waitSec * 1000);
                            while (ms > 0 &&
                                   !proc::shutdownRequested() &&
                                   !proc::cancelRequested()) {
                                int step = ms > 100 ? 100 : ms;
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(step));
                                ms -= step;
                            }
                        }
                    }
                    dt = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - t0).count();

                    {
                        std::lock_guard<std::mutex> lk(g_outMtx);
                        if (!captured.empty()) {
                            std::cout << captured;
                            if (captured.back() != '\n') std::cout << "\n";
                        }
                        if (rc == 0) printDone(t, dt);
                        else         printFail(t, rc);
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(statsMtx_);
                    stats_.tasks.push_back({taskName, dt, attempts, rc, fromCache});
                }

                runningCount.fetch_sub(1);
                done.fetch_add(1);

                               // ---- FAILURE → cancel all running siblings ----
                // Only the FIRST failure determines the final exit code.
                // Later failures are usually side-effects of the
                // cancellation itself (exit 130) and must NOT overwrite
                // the original error code.
                if (!fromCache && rc != 0) {
                    int expected = 0;
                    bool first = failed.compare_exchange_strong(
                        expected, rc,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire);

                    if (first && !opt_.silent) {
                        std::lock_guard<std::mutex> lk(g_outMtx);
                        std::cout << "  " << danger(sym::fail()) << " "
                                  << danger("cancelling remaining tasks")
                                  << "\n";
                        std::cout.flush();
                    }

                    proc::requestCancel();
                    return;
                }

                if (!fromCache && !opt_.noCache && !t.inputs.empty()) {
                    std::lock_guard<std::mutex> lk(cacheMtx);
                    store.entries[taskName] = cache::hashTask(t);
                    dirty = true;
                }

                std::lock_guard<std::mutex> lk(mtx);
                for (const auto& dep : rev[taskName])
                    if (--pending[dep] == 0) ready.push(dep);
            }
        });
    }

    for (auto& w : workers) w.join();

    // Save successes (partial progress is real; do not discard).
    if (dirty && !opt_.noCache) {
        try { store.save(cachePath); }
        catch (std::exception& e) {
            if (!opt_.silent && opt_.verbosity != Verbosity::Quiet)
                std::cerr << "  " << warn(sym::spark()) << " "
                          << warn(std::string("cache: ") + e.what()) << "\n";
        }
    }

    if (proc::shutdownRequested()) return 130;
    if (failed.load() != 0)        return failed.load();
    if (done.load() != total) {
        if (!opt_.silent)
            std::cerr << "  " << danger(sym::fail()) << " "
                      << danger("execution stalled — " +
                                std::to_string(total - done.load()) +
                                " task(s) unfinished") << "\n";
        return 1;
    }
    return 0;
}

} // namespace birun
// ============================================================
//  END OF FILE
// ============================================================