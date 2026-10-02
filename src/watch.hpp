#pragma once
// ============================================================
//  birun · watch.hpp  (v1.0.0)
//  - Linux: inotify-based event-driven wakeup (low CPU)
//  - Other POSIX: polling fallback (same API)
//  - Snapshot/diff logic stays the same for both paths
// ============================================================

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#  include <poll.h>
#  include <sys/inotify.h>
#  include <unistd.h>
#  include <cstring>
#  define BIRUN_HAS_INOTIFY 1
#endif

namespace birun::watch {

namespace fs = std::filesystem;

using Clock     = std::chrono::system_clock;
using TimePoint = fs::file_time_type;
using Snapshot  = std::map<std::string, TimePoint>;

// ------------------------------------------------------------
//  Filtering (unchanged)
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
//  Snapshot / diff (unchanged)
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

// ------------------------------------------------------------
//  Watcher — event-driven wakeup.
//
//  Usage:
//     Watcher w;
//     w.init(roots);          // best-effort; falls back to polling
//     for (;;) {
//         bool woke = w.wait(2000);  // ms
//         // ...do snapshot diff & maybe re-run...
//     }
//
//  `wait()` returns:
//     true   → an inotify event fired (or timeout in poll mode)
//     false  → only a poll timeout; caller may skip diffing
// ------------------------------------------------------------
class Watcher {
public:
    Watcher() = default;
    ~Watcher() { close(); }
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    bool init(const std::vector<std::string>& roots) {
#if defined(BIRUN_HAS_INOTIFY)
        fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd_ < 0) return false;

        for (const auto& r : roots) {
            std::error_code ec;
            if (fs::is_directory(r, ec)) {
                addRecursive(r);
            } else if (fs::is_regular_file(r, ec)) {
                addOne(fs::path(r).parent_path().string());
            }
        }
        return true;
#else
        (void)roots;
        return false;
#endif
    }

    bool wait(int timeoutMs) {
#if defined(BIRUN_HAS_INOTIFY)
        if (fd_ < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
            return true;  // poll mode: always diff
        }

        struct pollfd p{ fd_, POLLIN, 0 };
        int pr = ::poll(&p, 1, timeoutMs);
        if (pr <= 0) return false;

        // Drain the queue.
        char buf[8192]
            __attribute__((aligned(__alignof__(struct inotify_event))));
        ssize_t n;
        bool any = false;
        while ((n = ::read(fd_, buf, sizeof buf)) > 0) {
            any = true;
            for (char* ptr = buf; ptr < buf + n; ) {
                auto* ev = reinterpret_cast<struct inotify_event*>(ptr);
                if ((ev->mask & IN_ISDIR) && (ev->mask & IN_CREATE)) {
                    auto it = wdToPath_.find(ev->wd);
                    if (it != wdToPath_.end()) {
                        std::string sub = it->second;
                        if (!sub.empty() && sub.back() != '/') sub += '/';
                        sub += ev->name;
                        if (!isIgnored(sub + "/")) addRecursive(sub);
                    }
                }
                ptr += sizeof(struct inotify_event) + ev->len;
            }
        }
        return any;
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return true;
#endif
    }

    void close() {
#if defined(BIRUN_HAS_INOTIFY)
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        wdToPath_.clear();
#endif
    }

private:
#if defined(BIRUN_HAS_INOTIFY)
    int fd_ = -1;
    std::map<int, std::string> wdToPath_;

    void addOne(const std::string& dir) {
        if (dir.empty() || fd_ < 0) return;
        if (isIgnored(dir + "/")) return;

        uint32_t mask =
            IN_CREATE | IN_DELETE | IN_MODIFY |
            IN_MOVED_FROM | IN_MOVED_TO |
            IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

        int wd = ::inotify_add_watch(fd_, dir.c_str(), mask);
        if (wd >= 0) wdToPath_[wd] = dir;
    }

    void addRecursive(const std::string& dir) {
        addOne(dir);
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_directory(ec)) continue;
            std::string s = it->path().string();
            if (isIgnored(s + "/")) { it.disable_recursion_pending(); continue; }
            addOne(s);
        }
    }
#endif
};

} // namespace birun::watch