#pragma once
// ============================================================
//  birun · platform/fslock.hpp  (v1.0.0)
//  Cross-platform advisory file lock with exponential retry.
// ============================================================

#include <chrono>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace birun::platform {

class FileLock {
public:
    FileLock() = default;
    ~FileLock() { release(); }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    bool acquire(const std::string& path) {
        constexpr int delays[] = {25, 50, 100, 200, 400};

#ifdef _WIN32
        std::wstring wpath = utf8ToWide(path);
        h_ = CreateFileW(wpath.c_str(),
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) return false;

        for (int d : delays) {
            OVERLAPPED ov{};
            if (LockFileEx(h_,
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, MAXDWORD, MAXDWORD, &ov)) {
                held_ = true;
                return true;
            }
            DWORD err = GetLastError();
            if (err != ERROR_LOCK_VIOLATION && err != ERROR_IO_PENDING)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(d));
        }
        CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
        return false;
#else
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) return false;

        for (int d : delays) {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                held_ = true;
                return true;
            }
            if (errno != EWOULDBLOCK && errno != EINTR) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(d));
        }
        ::close(fd_);
        fd_ = -1;
        return false;
#endif
    }

    void release() {
#ifdef _WIN32
        if (held_ && h_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov{};
            UnlockFileEx(h_, 0, MAXDWORD, MAXDWORD, &ov);
            CloseHandle(h_);
        }
        h_ = INVALID_HANDLE_VALUE;
#else
        if (held_ && fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
        fd_ = -1;
#endif
        held_ = false;
    }

    bool held() const { return held_; }

private:
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;

    static std::wstring utf8ToWide(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    (int)s.size(), nullptr, 0);
        std::wstring w((size_t)n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(),
                            (int)s.size(), w.data(), n);
        return w;
    }
#else
    int fd_ = -1;
#endif
    bool held_ = false;
};

} // namespace birun::platform
