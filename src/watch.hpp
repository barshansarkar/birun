#pragma once
// ============================================================
//  birun · watch.hpp
//  Portable polling-based file watcher.
//  No platform-specific APIs — works on Linux, macOS, BSD.
// ============================================================

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace birun::watch {

namespace fs = std::filesystem;

using Clock    = std::chrono::system_clock;
using TimePoint = fs::file_time_type;
using Snapshot = std::map<std::string, TimePoint>;

// ------------------------------------------------------------
//  Path filter — skip noisy directories
// ------------------------------------------------------------
inline bool isIgnored(const std::string& path) {
    static const char* noisy[] = {
        "/.git/",           "/.hg/",       "/.svn/",
        "/build/",          "/.build/",    "/dist/",
        "/node_modules/",   "/.cache/",    "/.next/",
        "/target/",         "/__pycache__/",
        "/.venv/",          "/venv/",
        "/.idea/",          "/.vscode/",
        "\\\\.git\\\\",       "\\\\build\\\\",
        "\\\\node_modules\\\\",
    };
    for (auto* n : noisy)
        if (path.find(n) != std::string::npos) return true;
    return false;
}

// ------------------------------------------------------------
//  Only watch source-like files (reduces churn drastically)
// ------------------------------------------------------------
inline bool isWatchedExt(const fs::path& p) {
    static const char* exts[] = {
        ".bi",   ".sh",   ".bash", ".zsh",  ".fish",
        ".c",    ".h",    ".cc",   ".cpp",  ".hpp",
        ".cxx",  ".hxx",  ".rs",   ".go",   ".py",
        ".rb",   ".js",   ".jsx",  ".ts",   ".tsx",
        ".java", ".kt",   ".swift",".cs",   ".php",
        ".lua",  ".ex",   ".exs",  ".erl",  ".hs",
        ".ml",   ".scala",".clj",
        ".json", ".yaml", ".yml",  ".toml", ".ini",
        ".cfg",  ".conf", ".env",
        ".md",   ".rst",  ".txt",
        ".html", ".css",  ".scss", ".sass", ".less",
        ".sql",  ".proto",".graphql",
    };
    auto e = p.extension().string();
    for (auto* x : exts)
        if (e == x) return true;
    return false;
}

// ------------------------------------------------------------
//  Take a snapshot of all matching files under `roots`
// ------------------------------------------------------------
inline Snapshot snapshot(const std::vector<std::string>& roots) {
    Snapshot snap;
    std::error_code ec;

    for (const auto& r : roots) {
        if (isIgnored(r)) continue;

        if (fs::is_regular_file(r, ec)) {
            auto t = fs::last_write_time(r, ec);
            if (!ec) snap[r] = t;
            continue;
        }

        if (!fs::is_directory(r, ec)) continue;

        for (auto it = fs::recursive_directory_iterator(
                 r, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) { ec.clear(); continue; }

            const auto& p = it->path();
            std::string s = p.string();
            if (isIgnored(s)) {
                if (it->is_directory(ec))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            if (!isWatchedExt(p)) continue;

            auto t = fs::last_write_time(p, ec);
            if (!ec) snap[s] = t;
        }
    }
    return snap;
}

// ------------------------------------------------------------
//  Diff — return list of changed/added/removed paths
// ------------------------------------------------------------
inline std::vector<std::string> diff(const Snapshot& oldSnap,
                                     const Snapshot& newSnap) {
    std::vector<std::string> changed;

    for (const auto& [p, t] : newSnap) {
        auto it = oldSnap.find(p);
        if (it == oldSnap.end() || it->second != t)
            changed.push_back(p);
    }
    for (const auto& [p, t] : oldSnap) {
        if (!newSnap.count(p)) changed.push_back(p);
    }
    return changed;
}

} // namespace birun::watch