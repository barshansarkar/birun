#pragma once
// ============================================================
//  birun · builtins_birun.hpp
//  Super builtins that make birun a universal task runner.
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
    return ::stat(p.c_str(), &st) == 0 &&
           S_ISREG(st.st_mode) &&
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
        std::string dir = path.substr(
            start,
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
    using bi::vstr;
    using bi::vint;
    using bi::vbool;
    using bi::vnil;
    using bi::varr;
    using bi::vmap;
    using bi::truthy;
    using bi::def;

    // ============================================================
    //  Task definition builtins
    // ============================================================
    def(g, "desc", [](ValueList& a) -> Value {
        if (!g_currentTask)
            throw std::runtime_error(
                "desc() only inside `route TASK \"/...\" { ... }`");
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("desc(text): text must be a string");
        g_currentTask->desc = std::string(a[0].strView());
        return vnil();
    });

    def(g, "depends", [](ValueList& a) -> Value {
        if (!g_currentTask)
            throw std::runtime_error(
                "depends() only inside `route TASK \"/...\" { ... }`");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_currentTask->depends.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error(
                            "depends: array must contain strings");
                    g_currentTask->depends.push_back(std::string(x.strView()));
                }
            else
                throw std::runtime_error("depends: expected string or array");
        }
        return vnil();
    });

    def(g, "run", [](ValueList& a) -> Value {
        if (!g_currentTask)
            throw std::runtime_error(
                "run() only inside `route TASK \"/...\" { ... }`");
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("run(command): command must be a string");
        g_currentTask->runs.push_back(std::string(a[0].strView()));
        return vnil();
    });

    def(g, "inputs", [](ValueList& a) -> Value {
        if (!g_currentTask)
            throw std::runtime_error(
                "inputs() only inside `route TASK \"/...\" { ... }`");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_currentTask->inputs.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error(
                            "inputs: array must contain strings");
                    g_currentTask->inputs.push_back(std::string(x.strView()));
                }
            else
                throw std::runtime_error("inputs: expected string or array");
        }
        return vnil();
    });

    def(g, "outputs", [](ValueList& a) -> Value {
        if (!g_currentTask)
            throw std::runtime_error(
                "outputs() only inside `route TASK \"/...\" { ... }`");
        for (auto& v : a) {
            if (v.type == Value::STR)
                g_currentTask->outputs.push_back(std::string(v.strView()));
            else if (v.type == Value::ARR)
                for (auto& x : *v.arrPtr()) {
                    if (x.type != Value::STR)
                        throw std::runtime_error(
                            "outputs: array must contain strings");
                    g_currentTask->outputs.push_back(std::string(x.strView()));
                }
            else
                throw std::runtime_error("outputs: expected string or array");
        }
        return vnil();
    });

    // ============================================================
    //  sh(cmd) / sh(cmd, false)
    //    Runs through fork/exec; child gets its own process group.
    //    SIGINT/SIGTERM in parent → child is killed.
    // ============================================================
    def(g, "sh", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("sh(cmd): cmd must be a string");
        std::string cmd(a[0].strView());
        bool strict = true;
        if (a.size() > 1) strict = truthy(a[1]);

        std::string out;
        int code = proc::runCaptured(cmd, &out);
        if (strict && code != 0) {
            std::string msg = "sh: command failed (exit " +
                              std::to_string(code) + "): " + cmd;
            std::string t = detail::trim(out);
            if (!t.empty()) msg += "\n  " + t;
            throw std::runtime_error(msg);
        }
        return vstr(detail::trim(out));
    });

    // ============================================================
    //  shStatus(cmd) -> int
    // ============================================================
    def(g, "shStatus", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("shStatus(cmd): cmd must be a string");
        int code = proc::runSilent(std::string(a[0].strView()));
        return vint(code);
    });

    // ============================================================
    //  shFull(cmd) -> { code, out, err }
    //    Uses poll() on two pipes — no temp file, no leak.
    // ============================================================
    def(g, "shFull", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("shFull(cmd): cmd must be a string");

        std::string out, err;
        int code = proc::runSplit(std::string(a[0].strView()), &out, &err);

        auto m = std::make_shared<ValueMap>();
        (*m)["code"] = vint(code);
        (*m)["out"]  = vstr(detail::trim(out));
        (*m)["err"]  = vstr(detail::trim(err));
        return vmap(m);
    });

    // ============================================================
    //  which(name) -> full path or null
    // ============================================================
    def(g, "which", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("which(name): name must be a string");
        std::string p = detail::which(std::string(a[0].strView()));
        return p.empty() ? vnil() : vstr(p);
    });

    // ============================================================
    //  exists(path) -> bool
    // ============================================================
    def(g, "exists", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("exists(path): path must be a string");
        return vbool(detail::fileExists(std::string(a[0].strView())));
    });

    // ============================================================
    //  glob(pattern) -> array of paths
    //    Pure C++ — no shell, no injection surface.
    // ============================================================
    def(g, "glob", [](ValueList& a) -> Value {
        if (a.empty() || a[0].type != Value::STR)
            throw std::runtime_error("glob(pattern): pattern must be a string");
        std::string pat(a[0].strView());

        auto expanded = cache::expandGlob(pat);
        auto arr = std::make_shared<ValueList>();
        arr->reserve(expanded.size());
        for (auto& p : expanded) arr->push_back(vstr(p));
        return varr(arr);
    });

    // ============================================================
    //  pkg() -> detected package manager
    // ============================================================
    def(g, "pkg", [](ValueList&) -> Value {
        struct Rule { const char* file; const char* mgr; };
        static const Rule rules[] = {
            {"pnpm-lock.yaml",      "pnpm"},
            {"pnpm-workspace.yaml", "pnpm"},
            {"yarn.lock",           "yarn"},
            {"bun.lockb",           "bun"},
            {"package-lock.json",   "npm"},
            {"Cargo.toml",          "cargo"},
            {"uv.lock",             "uv"},
            {"poetry.lock",         "poetry"},
            {"pyproject.toml",      "pip"},
            {"requirements.txt",    "pip"},
            {"go.mod",              "go"},
            {"composer.json",       "composer"},
            {"pom.xml",             "maven"},
            {"build.gradle.kts",    "gradle"},
            {"build.gradle",        "gradle"},
            {"Gemfile",             "bundler"},
            {"mix.exs",             "mix"},
            {"pubspec.yaml",        "pub"},
        };
        for (auto& r : rules)
            if (detail::fileExists(r.file)) return vstr(r.mgr);
        return vnil();
    });

    // ============================================================
    //  cwd() / os() / arch() / inCI()
    // ============================================================
    def(g, "cwd", [](ValueList&) -> Value {
        char buf[4096];
        if (!getcwd(buf, sizeof buf)) return vstr("");
        return vstr(buf);
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
        return vbool(v != nullptr && *v != '\0' &&
                     std::strcmp(v, "false") != 0);
    });
}

} // namespace birun
// ============================================================
//  END OF FILE
// ============================================================