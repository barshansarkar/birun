#pragma once
// ============================================================
//  birun · builtins_birun.hpp  (v0.8.0)
// ============================================================

#include "bi/builtins.hpp"
#include "bi/interpreter.hpp"
#include "bi/value.hpp"
#include "cache.hpp"
#include "executor.hpp"
#include "proc.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace birun {

inline Task* g_currentTask = nullptr;

namespace detail {

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline bool isExecutable(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
           (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}
inline bool fileExists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}
inline std::string which(const std::string& name) {
    if (name.find('/') != std::string::npos)
        return isExecutable(name) ? name : "";
    const char* pe = std::getenv("PATH");
    if (!pe) return "";
    std::string path = pe;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        std::string dir = path.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty()) {
            std::string full = dir + "/" + name;
            if (isExecutable(full)) return full;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

} // namespace detail

inline void registerBirunBuiltins(std::shared_ptr<bi::Env> g) {
    using bi::Value;
    using bi::ValueList;
    using bi::ValueMap;
    using bi::vstr; using bi::vint; using bi::vbool;
    using bi::vnil; using bi::varr; using bi::vmap;
    using bi::truthy; using bi::def;

    // ============================================================
    //  Task metadata
    // ============================================================
    auto needTask = [](const char* fn) {
        if (!g_currentTask)
            throw std::runtime_error(
                std::string(fn) + "() only inside `route TASK \"/...\" { ... }`");
    };

    def(g, "desc", [needTask](ValueList& a) -> Value {
        needTask("desc");
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("desc(text): text must be a string");
        g_currentTask->desc = std::string(a[0].strView());
        return vnil();
    });

    def(g, "depends", [needTask](ValueList& a) -> Value {
        needTask("depends");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_currentTask->depends.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error("depends: array must contain strings");
                    g_currentTask->depends.push_back(std::string(x.strView()));
                }
            else throw std::runtime_error("depends: expected string or array");
        }
        return vnil();
    });

    def(g, "run", [needTask](ValueList& a) -> Value {
        needTask("run");
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("run(command): command must be a string");
        g_currentTask->runs.push_back(std::string(a[0].strView()));
        return vnil();
    });

    def(g, "inputs", [needTask](ValueList& a) -> Value {
        needTask("inputs");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_currentTask->inputs.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error("inputs: array must contain strings");
                    g_currentTask->inputs.push_back(std::string(x.strView()));
                }
            else throw std::runtime_error("inputs: expected string or array");
        }
        return vnil();
    });

    def(g, "outputs", [needTask](ValueList& a) -> Value {
        needTask("outputs");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_currentTask->outputs.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error("outputs: array must contain strings");
                    g_currentTask->outputs.push_back(std::string(x.strView()));
                }
            else throw std::runtime_error("outputs: expected string or array");
        }
        return vnil();
    });

    // ============================================================
    //  v0.8.0: timeout / retry / env / cwd
    // ============================================================
    def(g, "timeout", [needTask](ValueList& a) -> Value {
        needTask("timeout");
        if (a.empty())
            throw std::runtime_error("timeout(sec): sec required");
        int sec = (int)bi::toNum(a[0]);
        if (sec < 0) sec = 0;
        g_currentTask->timeoutSec = sec;
        return vnil();
    });

    def(g, "retry", [needTask](ValueList& a) -> Value {
        needTask("retry");
        if (a.empty())
            throw std::runtime_error("retry(n, backoffMs): n required");
        int n = (int)bi::toInt(a[0]);
        if (n < 0) n = 0;
        if (n > 100) n = 100;
        g_currentTask->retryCount = n;
        if (a.size() > 1) {
            int b = (int)bi::toNum(a[1]);
            if (b < 0) b = 0;
            g_currentTask->retryBackoffMs = b;
        }
        return vnil();
    });

    // env("K") / env("K", default) reads; inside a task env("K","V") sets.
    def(g, "env", [](ValueList& a) -> Value {
        if (a.empty()) return vstr("");
        if (g_currentTask && a.size() >= 2 && a[0].type == Value::STR) {
            g_currentTask->env.emplace_back(
                std::string(a[0].strView()),
                a[1].type == Value::STR ? std::string(a[1].strView())
                                        : bi::toStr(a[1]));
            return vnil();
        }
        std::string name(a[0].strView());
        const char* v = std::getenv(name.c_str());
        if (v) return vstr(v);
        return (a.size() > 1) ? a[1] : vnil();
    });

    // cwd() returns; inside a task cwd("path") sets.
    def(g, "cwd", [](ValueList& a) -> Value {
        if (g_currentTask && !a.empty() && a[0].type == Value::STR) {
            g_currentTask->cwd = std::string(a[0].strView());
            return vnil();
        }
        char buf[4096];
        if (!getcwd(buf, sizeof buf)) return vstr("");
        return vstr(buf);
    });

    // ============================================================
    //  sh / shStatus / shFull — pass through per-task timeout/env/cwd
    // ============================================================
    auto currentOpts = [](int extraMs = 0) {
        proc::RunOpts ro;
        if (g_currentTask) {
            ro.timeoutMs = g_currentTask->timeoutSec > 0
                         ? g_currentTask->timeoutSec * 1000 : 0;
            ro.env = g_currentTask->env;
            ro.cwd = g_currentTask->cwd;
        }
        if (extraMs > 0) ro.timeoutMs = extraMs;
        return ro;
    };

    def(g, "sh", [currentOpts](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("sh(cmd): cmd must be a string");
        std::string cmd(a[0].strView());
        bool strict = true;
        if (a.size() > 1) strict = truthy(a[1]);

        std::string out;
        auto r = proc::runCapturedEx(cmd, &out, currentOpts());
        if (r.timedOut)
            throw std::runtime_error("sh: command timed out: " + cmd);
        if (strict && r.exitCode != 0) {
            std::string msg = "sh: command failed (exit " +
                              std::to_string(r.exitCode) + "): " + cmd;
            std::string t = detail::trim(out);
            if (!t.empty()) msg += "\n  " + t;
            throw std::runtime_error(msg);
        }
        return vstr(detail::trim(out));
    });

    def(g, "shStatus", [currentOpts](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("shStatus(cmd): cmd must be a string");
        auto r = proc::runSilentEx(std::string(a[0].strView()), currentOpts());
        return vint(r.timedOut ? proc::kExitTimeout : r.exitCode);
    });

    def(g, "shFull", [currentOpts](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("shFull(cmd): cmd must be a string");
        std::string out, err;
        auto r = proc::runSplitEx(std::string(a[0].strView()), &out, &err,
                                  currentOpts());
        auto m = std::make_shared<ValueMap>();
        (*m)["code"]     = vint(r.timedOut ? proc::kExitTimeout : r.exitCode);
        (*m)["out"]      = vstr(detail::trim(out));
        (*m)["err"]      = vstr(detail::trim(err));
        (*m)["timedOut"] = vbool(r.timedOut);
        return vmap(m);
    });

    // ============================================================
    //  filesystem / system helpers
    // ============================================================
    def(g, "which", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("which(name): name must be a string");
        std::string p = detail::which(std::string(a[0].strView()));
        return p.empty() ? vnil() : vstr(p);
    });

    def(g, "exists", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("exists(path): path must be a string");
        return vbool(detail::fileExists(std::string(a[0].strView())));
    });

    def(g, "glob", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("glob(pattern): pattern must be a string");
        auto expanded = cache::expandGlob(std::string(a[0].strView()));
        auto arr = std::make_shared<ValueList>();
        arr->reserve(expanded.size());
        for (auto& p : expanded) arr->push_back(vstr(p));
        return varr(arr);
    });

    def(g, "pkg", [](ValueList&) -> Value {
        struct Rule { const char* file; const char* mgr; };
        static const Rule rules[] = {
            {"pnpm-lock.yaml","pnpm"},{"pnpm-workspace.yaml","pnpm"},
            {"yarn.lock","yarn"},{"bun.lockb","bun"},
            {"package-lock.json","npm"},{"Cargo.toml","cargo"},
            {"uv.lock","uv"},{"poetry.lock","poetry"},
            {"pyproject.toml","pip"},{"requirements.txt","pip"},
            {"go.mod","go"},{"composer.json","composer"},
            {"pom.xml","maven"},{"build.gradle.kts","gradle"},
            {"build.gradle","gradle"},{"Gemfile","bundler"},
            {"mix.exs","mix"},{"pubspec.yaml","pub"},
        };
        for (auto& r : rules)
            if (detail::fileExists(r.file)) return vstr(r.mgr);
        return vnil();
    });

    def(g, "os", [](ValueList&) -> Value {
#if defined(__linux__)
        return vstr("linux");
#elif defined(__APPLE__)
        return vstr("darwin");
#elif defined(_WIN32)
        return vstr("windows");
#else
        return vstr("unknown");
#endif
    });

    def(g, "arch", [](ValueList&) -> Value {
#if defined(__x86_64__) || defined(_M_X64)
        return vstr("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
        return vstr("arm64");
#elif defined(__i386__) || defined(_M_IX86)
        return vstr("x86");
#elif defined(__arm__) || defined(_M_ARM)
        return vstr("arm");
#else
        return vstr("unknown");
#endif
    });

    def(g, "inCI", [](ValueList&) -> Value {
        const char* v = std::getenv("CI");
        if (!v) v = std::getenv("CONTINUOUS_INTEGRATION");
        return vbool(v && *v && std::strcmp(v, "false") != 0);
    });
}

} // namespace birun
// ============================================================
//  END OF FILE
// ============================================================