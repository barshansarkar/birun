#pragma once
// ============================================================
//  birun · cache.hpp  (v1.0.0)
//  Task caching: input file hashes + task definition.
//  - versioned file format (BIRUN_CACHE_MAGIC)
//  - advisory flock() WITH RETRY — never overwrites under lock failure
//  - atomic write via temp file + rename
// ============================================================

#include "executor.hpp"
#include "version.hpp"
#include "platform/fslock.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
// #include <sys/file.h>
// #include <fcntl.h>
#include <thread>
// #include <unistd.h>
#include <vector>

namespace birun::cache {

namespace fs = std::filesystem;

// ------------------------------------------------------------
//  FNV-1a 64-bit
// ------------------------------------------------------------
inline uint64_t fnv1a(const void* data, size_t len,
                      uint64_t h = 1469598103934665603ULL) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

inline uint64_t hashFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a(path.data(), path.size(), h);
    char buf[8192];
    while (f) {
        f.read(buf, sizeof buf);
        std::streamsize n = f.gcount();
        if (n <= 0) break;
        h = fnv1a(buf, (size_t)n, h);
    }
    return h;
}

// ------------------------------------------------------------
//  Wildcard / glob (unchanged)
// ------------------------------------------------------------
inline bool wildcardMatch(const std::string& name, const std::string& pat) {
    size_t ni = 0, pi = 0;
    size_t star = std::string::npos, match = 0;
    while (ni < name.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == name[ni])) {
            ni++; pi++;
        } else if (pi < pat.size() && pat[pi] == '*') {
            star = pi++;
            match = ni;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ni = ++match;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '*') pi++;
    return pi == pat.size();
}

inline std::vector<std::string> expandGlob(const std::string& pattern) {
    std::vector<std::string> out;
    std::error_code ec;

    auto dstar = pattern.find("**");
    if (dstar != std::string::npos) {
        std::string root = pattern.substr(0, dstar);
        if (!root.empty() && root.back() == '/') root.pop_back();
        if (root.empty()) root = ".";

        std::string rest = pattern.substr(dstar + 2);
        if (!rest.empty() && rest[0] == '/') rest = rest.substr(1);

        if (!fs::is_directory(root, ec)) return out;

        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;
            std::string full = it->path().string();
            std::string base = it->path().filename().string();
            if (rest.empty() || wildcardMatch(base, rest))
                out.push_back(full);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    if (pattern.find('*') != std::string::npos ||
        pattern.find('?') != std::string::npos) {
        auto slash = pattern.find_last_of('/');
        std::string dir   = (slash == std::string::npos) ? "." : pattern.substr(0, slash);
        std::string fileP = (slash == std::string::npos) ? pattern : pattern.substr(slash + 1);

        if (!fs::is_directory(dir, ec)) return out;
        for (auto& p : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!p.is_regular_file()) continue;
            auto fname = p.path().filename().string();
            if (wildcardMatch(fname, fileP))
                out.push_back(p.path().string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    if (fs::is_regular_file(pattern, ec))
        out.push_back(pattern);
    return out;
}

// ------------------------------------------------------------
//  Hash a set of glob patterns → hex string
// ------------------------------------------------------------
inline std::string hashInputs(const std::vector<std::string>& globs) {
    std::vector<std::string> files;
    for (const auto& g : globs) {
        auto expanded = expandGlob(g);
        files.insert(files.end(), expanded.begin(), expanded.end());
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    uint64_t combined = 1469598103934665603ULL;
    for (const auto& f : files) {
        uint64_t h = hashFile(f);
        combined = fnv1a(&h, sizeof h, combined);
    }

    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)combined);
    return std::string(buf);
}

// ------------------------------------------------------------
//  Hash task definition + input files.
//  Cache-format-versioned: any change to the mixing rules must
//  bump BIRUN_CACHE_FORMAT in version.hpp.
// ------------------------------------------------------------
inline std::string hashTask(const Task& t) {
    uint64_t combined = 1469598103934665603ULL;

    // Seed with cache format version — invalidates old cache when bumped.
    const char* fmt = BIRUN_CACHE_MAGIC;
    combined = fnv1a(fmt, std::strlen(fmt), combined);

    auto mixStr = [&combined](const std::string& s) {
        combined = fnv1a(s.data(), s.size(), combined);
        const char sep = '\x1f';
        combined = fnv1a(&sep, 1, combined);
    };

    mixStr(t.desc);
    for (const auto& c : t.runs)    mixStr(c);
    for (const auto& d : t.depends) mixStr(d);
    for (const auto& o : t.outputs) mixStr(o);
    for (const auto& kv : t.env) { mixStr(kv.first); mixStr(kv.second); }
    mixStr(t.cwd);
    mixStr(std::to_string(t.timeoutSec));
    mixStr(std::to_string(t.retryCount));
    mixStr(std::to_string(t.retryBackoffMs));

    std::vector<std::string> files;
    for (const auto& g : t.inputs) {
        auto expanded = expandGlob(g);
        files.insert(files.end(), expanded.begin(), expanded.end());
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    for (const auto& f : files) {
        uint64_t h = hashFile(f);
        combined = fnv1a(&h, sizeof h, combined);
    }

    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)combined);
    return std::string(buf);
}

// ------------------------------------------------------------
//  Outputs exist?
// ------------------------------------------------------------
inline bool outputsExist(const std::vector<std::string>& outputs) {
    if (outputs.empty()) return true;
    std::error_code ec;
    for (const auto& o : outputs) {
        auto expanded = expandGlob(o);
        if (expanded.empty()) {
            if (!fs::exists(o, ec)) return false;
        }
    }
    return true;
}

// ------------------------------------------------------------
//  Advisory file lock with RETRY.
//  - LOCK_EX (exclusive) for writes
//  - backoff: 5 attempts × (25, 50, 100, 200, 400 ms) = ~775 ms
// ------------------------------------------------------------


// ------------------------------------------------------------
//  Cache store — versioned format.
//
//    Line 1:  BIRUN_CACHE_MAGIC   (e.g. "birun-cache-v2")
//    Rest:    key \t value
//
//  On load, if the magic doesn't match → treat as empty (silent
//  upgrade; old cache is simply discarded).
// ------------------------------------------------------------
struct CacheStore {
    std::map<std::string, std::string> entries;

    static CacheStore load(const std::string& path) {
        CacheStore c;
        std::ifstream f(path, std::ios::binary);
        if (!f) return c;

        std::string first;
        if (!std::getline(f, first)) return c;
        if (first != BIRUN_CACHE_MAGIC) {
            // Version mismatch — invalidate everything.
            return c;
        }

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            c.entries[line.substr(0, tab)] = line.substr(tab + 1);
        }
        return c;
    }

    // Throws if the lock cannot be acquired. This is INTENTIONAL —
    // silently overwriting a locked cache could lose another
    // process's writes.
    void save(const std::string& path) const {
               platform::FileLock lk;
        if (!lk.acquire(path)) {
            throw std::runtime_error(
                "cache: could not acquire lock on '" + path +
                ".lock' (another birun is writing?)");
        }

        auto dir = fs::path(path).parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            fs::create_directories(dir, ec);
        }
#ifdef _WIN32
        std::string tmp = path + ".tmp." + std::to_string((unsigned long)GetCurrentProcessId());
#else
        std::string tmp = path + ".tmp." + std::to_string(::getpid());
#endif

        bool ok = true;
        {
            std::ofstream f(tmp, std::ios::trunc | std::ios::binary);
            if (!f) {
                ok = false;
            } else {
                f << BIRUN_CACHE_MAGIC << '\n';
                for (const auto& kv : entries)
                    f << kv.first << '\t' << kv.second << '\n';
                f.flush();
                if (!f) ok = false;
            }
        }

        if (!ok) {
            std::error_code ec;
            fs::remove(tmp, ec);
            throw std::runtime_error("cache: cannot write '" + path + "'");
        }

        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (ec) {
            std::error_code ec2;
            fs::remove(tmp, ec2);
            throw std::runtime_error(
                "cache: cannot replace '" + path + "': " + ec.message());
        }
    }
};

// ------------------------------------------------------------
//  Cache file path
// ------------------------------------------------------------
inline std::string cacheFilePath() {
    const char* env = std::getenv("BIRUN_CACHE_FILE");
    if (env && *env) return env;
    return ".birun/cache";
}

// ------------------------------------------------------------
//  Clean
// ------------------------------------------------------------
inline bool cleanCache() {
    std::string path = cacheFilePath();
    std::error_code ec;
    bool removed = false;
    if (fs::exists(path, ec)) removed = fs::remove(path, ec);
    std::error_code ec2;
    fs::remove(path + ".lock", ec2);
    return removed;
}

} // namespace birun::cache