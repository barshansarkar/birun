#pragma once
// ============================================================
//  birun · proc.hpp  (v0.8.0)
//  - fork/exec, no raw system()/popen
//  - per-task env / cwd / timeout / SIGTERM->SIGKILL grace
//  - dynamic-size child registry (up to 4096)
//  - graceful shutdown: 1st signal = SIGTERM, 2nd = SIGKILL
// ============================================================

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio> 
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace birun::proc {

constexpr size_t kMaxChildren   = 4096;
constexpr int    kDefaultGraceMs = 3000;
constexpr int    kExitTimeout    = 124;

inline std::atomic<pid_t> g_pids[kMaxChildren] = {};
inline std::atomic<bool>  g_shutdown{false};
inline std::atomic<int>   g_shutdownSig{0};

inline void registerChild(pid_t p) {
    for (size_t i = 0; i < kMaxChildren; i++) {
        pid_t e = 0;
        if (g_pids[i].compare_exchange_strong(e, p,
                std::memory_order_acq_rel)) return;
    }
    // registry full — child still runs, just not trackable
}
inline void unregisterChild(pid_t p) {
    for (size_t i = 0; i < kMaxChildren; i++) {
        pid_t e = p;
        g_pids[i].compare_exchange_strong(e, 0,
            std::memory_order_acq_rel);
    }
}
inline void killAll(int sig) {
    for (size_t i = 0; i < kMaxChildren; i++) {
        pid_t p = g_pids[i].load(std::memory_order_acquire);
        if (p > 0) { ::kill(-p, sig); ::kill(p, sig); }
    }
}

inline void signalHandler(int sig) {
    int prev = g_shutdownSig.exchange(sig, std::memory_order_relaxed);
    g_shutdown.store(true, std::memory_order_release);
    if (prev != 0) killAll(SIGKILL);       // 2nd signal → immediate
    else           killAll(SIGTERM);       // 1st → graceful
}

inline void reaperThread() {
    while (!g_shutdown.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultGraceMs));
    killAll(SIGKILL);
}

inline void installSignalHandlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGHUP,  &sa, nullptr);
    ::signal(SIGPIPE, SIG_IGN);
    std::thread(reaperThread).detach();
}

inline bool shutdownRequested() { return g_shutdown.load(std::memory_order_acquire); }
inline int  shutdownSignal()    { return g_shutdownSig.load(std::memory_order_relaxed); }

// ------------------------------------------------------------
//  Run options / result
// ------------------------------------------------------------
struct RunOpts {
    int timeoutMs = 0;          // 0 = no timeout
    int graceMs   = kDefaultGraceMs;
    std::vector<std::pair<std::string, std::string>> env;
    std::string cwd;
};

struct RunResult {
    int    exitCode  = -1;
    bool   timedOut  = false;
    bool   signalled = false;
    int    signal    = 0;
    double seconds   = 0;
};

// ------------------------------------------------------------
//  Common helpers
// ------------------------------------------------------------
inline void applyChildSetup(const RunOpts& o) {
    ::setpgid(0, 0);
    if (!o.cwd.empty()) {
        if (::chdir(o.cwd.c_str()) != 0) {
            // child — nothing sensible to do; print & bail
            std::fprintf(stderr, "birun: chdir('%s'): %s\n",
                         o.cwd.c_str(), std::strerror(errno));
            ::_exit(126);
        }
    }
    for (auto& kv : o.env) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
}

inline double elapsedMs(std::chrono::steady_clock::time_point s) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - s).count();
}

// ------------------------------------------------------------
//  Live: child inherits our stdio (streams to terminal)
// ------------------------------------------------------------
inline RunResult runLiveEx(const std::string& cmd, const RunOpts& o) {
    RunResult r;
    auto start = std::chrono::steady_clock::now();

    pid_t pid = ::fork();
    if (pid < 0) return r;
    if (pid == 0) {
        applyChildSetup(o);
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);

    bool killed = false, timedOut = false;
    auto graceEnd = std::chrono::steady_clock::time_point::max();
    int status = 0;

    for (;;) {
        pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) { unregisterChild(pid); return r; }

        auto now = std::chrono::steady_clock::now();

        if (!killed && o.timeoutMs > 0 &&
            elapsedMs(start) >= (double)o.timeoutMs) {
            timedOut = true; killed = true;
            ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (!killed && shutdownRequested()) {
            killed = true;
            ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (killed && now >= graceEnd) ::kill(-pid, SIGKILL);

        ::usleep(50 * 1000);
    }
    unregisterChild(pid);

    r.timedOut = timedOut;
    r.seconds  = elapsedMs(start) / 1000.0;
    if (timedOut) { r.exitCode = kExitTimeout; r.signalled = true; r.signal = SIGTERM; }
    else if (WIFEXITED(status))   r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) { r.signalled = true; r.signal = WTERMSIG(status);
                                    r.exitCode = 128 + r.signal; }
    return r;
}

inline int runLive(const std::string& cmd) {
    return runLiveEx(cmd, {}).exitCode;
}

// ------------------------------------------------------------
//  Captured: combined stdout+stderr into *out (poll + timeout)
// ------------------------------------------------------------
inline RunResult runCapturedEx(const std::string& cmd, std::string* out,
                               const RunOpts& o) {
    RunResult r;
    auto start = std::chrono::steady_clock::now();

    int pfd[2];
    if (::pipe(pfd) < 0) return r;

    pid_t pid = ::fork();
    if (pid < 0) { ::close(pfd[0]); ::close(pfd[1]); return r; }
    if (pid == 0) {
        applyChildSetup(o);
        ::close(pfd[0]);
        ::dup2(pfd[1], STDOUT_FILENO);
        ::dup2(pfd[1], STDERR_FILENO);
        ::close(pfd[1]);
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);
    ::close(pfd[1]);

    int openFd = pfd[0];
    bool killed = false, timedOut = false, reaped = false;
    auto graceEnd = std::chrono::steady_clock::time_point::max();
    int status = 0;
    char buf[4096];

    while (openFd >= 0 || !reaped) {
        auto now = std::chrono::steady_clock::now();

        if (!killed && o.timeoutMs > 0 &&
            elapsedMs(start) >= (double)o.timeoutMs) {
            timedOut = true; killed = true;
            ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (!killed && shutdownRequested()) {
            killed = true;
            ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (killed && now >= graceEnd) ::kill(-pid, SIGKILL);

        int pollMs = 50;
        if (!killed && o.timeoutMs > 0) {
            int rem = o.timeoutMs - (int)elapsedMs(start);
            if (rem > 0 && rem < pollMs) pollMs = rem;
        }

        if (openFd >= 0) {
            struct pollfd p{ openFd, POLLIN, 0 };
            int pr = ::poll(&p, 1, pollMs);
            if (pr > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = ::read(openFd, buf, sizeof buf);
                if (n > 0) { if (out) out->append(buf, (size_t)n); }
                else if (n == 0) { ::close(openFd); openFd = -1; }
                else if (errno != EINTR) { ::close(openFd); openFd = -1; }
            }
        } else ::usleep(20 * 1000);

        if (!reaped) {
            pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid) reaped = true;
            else if (w < 0 && errno != EINTR) reaped = true;
        }
    }
    if (openFd >= 0) ::close(openFd);
    unregisterChild(pid);

    r.timedOut = timedOut;
    r.seconds  = elapsedMs(start) / 1000.0;
    if (timedOut) { r.exitCode = kExitTimeout; r.signalled = true; r.signal = SIGTERM; }
    else if (WIFEXITED(status))   r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) { r.signalled = true; r.signal = WTERMSIG(status);
                                    r.exitCode = 128 + r.signal; }
    return r;
}

inline int runCaptured(const std::string& cmd, std::string* out) {
    return runCapturedEx(cmd, out, {}).exitCode;
}

// ------------------------------------------------------------
//  Silent: discard all output
// ------------------------------------------------------------
inline RunResult runSilentEx(const std::string& cmd, const RunOpts& o) {
    RunResult r;
    auto start = std::chrono::steady_clock::now();

    pid_t pid = ::fork();
    if (pid < 0) return r;
    if (pid == 0) {
        applyChildSetup(o);
        int nf = ::open("/dev/null", O_WRONLY);
        if (nf >= 0) { ::dup2(nf, STDOUT_FILENO); ::dup2(nf, STDERR_FILENO);
                       ::close(nf); }
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);

    bool killed = false, timedOut = false;
    auto graceEnd = std::chrono::steady_clock::time_point::max();
    int status = 0;
    for (;;) {
        pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) { unregisterChild(pid); return r; }
        auto now = std::chrono::steady_clock::now();
        if (!killed && o.timeoutMs > 0 &&
            elapsedMs(start) >= (double)o.timeoutMs) {
            timedOut = true; killed = true;
            ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (!killed && shutdownRequested()) {
            killed = true; ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (killed && now >= graceEnd) ::kill(-pid, SIGKILL);
        ::usleep(50 * 1000);
    }
    unregisterChild(pid);

    r.timedOut = timedOut;
    r.seconds  = elapsedMs(start) / 1000.0;
    if (timedOut) r.exitCode = kExitTimeout;
    else if (WIFEXITED(status))   r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exitCode = 128 + WTERMSIG(status);
    return r;
}

inline int runSilent(const std::string& cmd) {
    return runSilentEx(cmd, {}).exitCode;
}

// ------------------------------------------------------------
//  Split: separate stdout / stderr (poll both)
// ------------------------------------------------------------
inline RunResult runSplitEx(const std::string& cmd,
                            std::string* out, std::string* err,
                            const RunOpts& o) {
    RunResult r;
    auto start = std::chrono::steady_clock::now();

    int op[2] = {-1,-1}, ep[2] = {-1,-1};
    bool hasO = (out != nullptr), hasE = (err != nullptr);
    if (hasO && ::pipe(op) < 0) return r;
    if (hasE && ::pipe(ep) < 0) { if (hasO){::close(op[0]);::close(op[1]);} return r; }

    pid_t pid = ::fork();
    if (pid < 0) {
        if (hasO){::close(op[0]);::close(op[1]);}
        if (hasE){::close(ep[0]);::close(ep[1]);}
        return r;
    }
    if (pid == 0) {
        applyChildSetup(o);
        if (hasO) { ::close(op[0]); ::dup2(op[1], STDOUT_FILENO); ::close(op[1]); }
        else      { int nf=::open("/dev/null",O_WRONLY); if(nf>=0){::dup2(nf,STDOUT_FILENO);::close(nf);} }
        if (hasE) { ::close(ep[0]); ::dup2(ep[1], STDERR_FILENO); ::close(ep[1]); }
        else      { int nf=::open("/dev/null",O_WRONLY); if(nf>=0){::dup2(nf,STDERR_FILENO);::close(nf);} }
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);
    if (hasO) ::close(op[1]);
    if (hasE) ::close(ep[1]);

    struct pollfd pfds[2];
    int nfds = 0, oi = -1, ei = -1;
    if (hasO) { pfds[nfds].fd=op[0]; pfds[nfds].events=POLLIN; pfds[nfds].revents=0; oi=nfds++; }
    if (hasE) { pfds[nfds].fd=ep[0]; pfds[nfds].events=POLLIN; pfds[nfds].revents=0; ei=nfds++; }
    int openFds = nfds;

    bool killed=false, timedOut=false, reaped=false;
    auto graceEnd = std::chrono::steady_clock::time_point::max();
    int status = 0;
    char buf[4096];

    while (openFds > 0 || !reaped) {
        auto now = std::chrono::steady_clock::now();
        if (!killed && o.timeoutMs > 0 &&
            elapsedMs(start) >= (double)o.timeoutMs) {
            timedOut = true; killed = true;
            ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (!killed && shutdownRequested()) {
            killed = true; ::kill(-pid, SIGTERM);
            graceEnd = now + std::chrono::milliseconds(o.graceMs);
        }
        if (killed && now >= graceEnd) ::kill(-pid, SIGKILL);

        int pollMs = 50;
        if (!killed && o.timeoutMs > 0) {
            int rem = o.timeoutMs - (int)elapsedMs(start);
            if (rem > 0 && rem < pollMs) pollMs = rem;
        }

        if (openFds > 0) {
            int pr = ::poll(pfds, (nfds_t)nfds, pollMs);
            if (pr > 0) {
                for (int i = 0; i < nfds; i++) {
                    if (pfds[i].fd < 0) continue;
                    if (pfds[i].revents & (POLLIN|POLLHUP|POLLERR)) {
                        ssize_t n = ::read(pfds[i].fd, buf, sizeof buf);
                        if (n > 0) {
                            if (i == oi && out) out->append(buf, (size_t)n);
                            else if (i == ei && err) err->append(buf, (size_t)n);
                        } else if (n == 0) { ::close(pfds[i].fd); pfds[i].fd=-1; openFds--; }
                        else if (errno != EINTR) { ::close(pfds[i].fd); pfds[i].fd=-1; openFds--; }
                    }
                }
            }
        } else ::usleep(20 * 1000);

        if (!reaped) {
            pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid) reaped = true;
            else if (w < 0 && errno != EINTR) reaped = true;
        }
    }

    r.timedOut = timedOut;
    r.seconds  = elapsedMs(start) / 1000.0;
    if (timedOut) r.exitCode = kExitTimeout;
    else if (WIFEXITED(status))   r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exitCode = 128 + WTERMSIG(status);
    return r;
}

inline int runSplit(const std::string& cmd, std::string* o, std::string* e) {
    return runSplitEx(cmd, o, e, {}).exitCode;
}

} // namespace birun::proc