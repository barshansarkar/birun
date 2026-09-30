#pragma once
// ============================================================
//  birun · proc.hpp
//  Safe process execution:
//    - fork/exec (no raw shell-string via system/popen)
//    - child process group tracking
//    - SIGINT/SIGTERM/SIGHUP forwarding → no orphan processes
// ============================================================

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace birun::proc {

// ------------------------------------------------------------
//  Child PID registry (lock-free, async-signal-safe)
// ------------------------------------------------------------
constexpr size_t kMaxChildren = 512;

inline std::atomic<pid_t> g_pids[kMaxChildren] = {};
inline std::atomic<bool>  g_shutdown{false};
inline std::atomic<int>   g_shutdownSig{0};

inline void registerChild(pid_t p) {
    for (size_t i = 0; i < kMaxChildren; i++) {
        pid_t expected = 0;
        if (g_pids[i].compare_exchange_strong(expected, p,
                                              std::memory_order_acq_rel))
            return;
    }
}

inline void unregisterChild(pid_t p) {
    for (size_t i = 0; i < kMaxChildren; i++) {
        pid_t expected = p;
        g_pids[i].compare_exchange_strong(expected, 0,
                                          std::memory_order_acq_rel);
    }
}

inline void killAll(int sig) {
    for (size_t i = 0; i < kMaxChildren; i++) {
        pid_t p = g_pids[i].load(std::memory_order_acquire);
        if (p > 0) {
            ::kill(-p, sig);   // whole process group
            ::kill(p, sig);    // fallback
        }
    }
}

inline void signalHandler(int sig) {
    g_shutdownSig.store(sig, std::memory_order_relaxed);
    g_shutdown.store(true, std::memory_order_release);
    killAll(SIGTERM);
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
}

inline bool shutdownRequested() {
    return g_shutdown.load(std::memory_order_acquire);
}
inline int shutdownSignal() {
    return g_shutdownSig.load(std::memory_order_relaxed);
}

// ------------------------------------------------------------
//  Drain fd into string
// ------------------------------------------------------------
inline void drainFd(int fd, std::string* out) {
    if (fd < 0) return;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n > 0) {
            if (out) out->append(buf, (size_t)n);
        } else if (n == 0) break;
        else if (errno != EINTR) break;
    }
}

// ------------------------------------------------------------
//  Wait for child, return exit code (or 128+signum)
// ------------------------------------------------------------
inline int waitFor(pid_t pid) {
    int status = 0;
    for (;;) {
        pid_t r = ::waitpid(pid, &status, 0);
        if (r == pid) break;
        if (r < 0 && errno == EINTR) {
            if (shutdownRequested()) ::kill(-pid, SIGKILL);
            continue;
        }
        unregisterChild(pid);
        return -1;
    }
    unregisterChild(pid);
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

// ------------------------------------------------------------
//  Fork helper — child gets its own process group
// ------------------------------------------------------------
inline pid_t spawnChild(const std::string& cmd) {
    pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        ::setpgid(0, 0);
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);
    return pid;
}

// ------------------------------------------------------------
//  Live: stream child stdout/stderr to terminal
// ------------------------------------------------------------
inline int runLive(const std::string& cmd) {
    pid_t pid = spawnChild(cmd);
    if (pid < 0) return -1;
    return waitFor(pid);
}

// ------------------------------------------------------------
//  Captured: combined stdout+stderr into `out`
// ------------------------------------------------------------
inline int runCaptured(const std::string& cmd, std::string* out) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = ::fork();
    if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return -1; }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);
    ::close(pipefd[1]);
    drainFd(pipefd[0], out);
    ::close(pipefd[0]);
    return waitFor(pid);
}

// ------------------------------------------------------------
//  Silent: discard output, return only exit code
// ------------------------------------------------------------
inline int runSilent(const std::string& cmd) {
    pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        ::setpgid(0, 0);
        int nullfd = ::open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            ::dup2(nullfd, STDOUT_FILENO);
            ::dup2(nullfd, STDERR_FILENO);
            ::close(nullfd);
        }
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);
    return waitFor(pid);
}

// ------------------------------------------------------------
//  Split: separate stdout & stderr via poll()
//         NO temp files → no leaks.
// ------------------------------------------------------------
inline int runSplit(const std::string& cmd,
                    std::string* out, std::string* err) {
    bool hasOut = (out != nullptr);
    bool hasErr = (err != nullptr);

    int opipe[2] = {-1, -1};
    int epipe[2] = {-1, -1};
    if (hasOut && pipe(opipe) < 0) return -1;
    if (hasErr && pipe(epipe) < 0) {
        if (hasOut) { ::close(opipe[0]); ::close(opipe[1]); }
        return -1;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        if (hasOut) { ::close(opipe[0]); ::close(opipe[1]); }
        if (hasErr) { ::close(epipe[0]); ::close(epipe[1]); }
        return -1;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        if (hasOut) {
            ::close(opipe[0]);
            ::dup2(opipe[1], STDOUT_FILENO);
            ::close(opipe[1]);
        } else {
            int nf = ::open("/dev/null", O_WRONLY);
            if (nf >= 0) { ::dup2(nf, STDOUT_FILENO); ::close(nf); }
        }
        if (hasErr) {
            ::close(epipe[0]);
            ::dup2(epipe[1], STDERR_FILENO);
            ::close(epipe[1]);
        } else {
            int nf = ::open("/dev/null", O_WRONLY);
            if (nf >= 0) { ::dup2(nf, STDERR_FILENO); ::close(nf); }
        }
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        ::_exit(127);
    }
    registerChild(pid);
    if (hasOut) ::close(opipe[1]);
    if (hasErr) ::close(epipe[1]);

    struct pollfd pfds[2];
    int nfds = 0, outIdx = -1, errIdx = -1;

    if (hasOut) {
        pfds[nfds].fd = opipe[0];
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        outIdx = nfds++;
    }
    if (hasErr) {
        pfds[nfds].fd = epipe[0];
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        errIdx = nfds++;
    }

    int openFds = nfds;
    char buf[4096];
    while (openFds > 0) {
        int r = ::poll(pfds, (nfds_t)nfds, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (pfds[i].fd < 0) continue;
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = ::read(pfds[i].fd, buf, sizeof buf);
                if (n > 0) {
                    if (i == outIdx && out) out->append(buf, (size_t)n);
                    else if (i == errIdx && err) err->append(buf, (size_t)n);
                } else if (n == 0) {
                    ::close(pfds[i].fd);
                    pfds[i].fd = -1;
                    openFds--;
                } else if (errno != EINTR) {
                    ::close(pfds[i].fd);
                    pfds[i].fd = -1;
                    openFds--;
                }
            }
        }
    }

    return waitFor(pid);
}

} // namespace birun::proc