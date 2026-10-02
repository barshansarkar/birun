#pragma once
// ============================================================
//  birun · platform/proc_win32.hpp  (v1.0.0)
//  Windows process spawning:
//    - CreateProcessW with UTF-8 → UTF-16 conversion
//    - Job Object for atomic tree-kill (better than POSIX pgroup)
//    - SetConsoleCtrlHandler for Ctrl+C / Ctrl+Break
//    - cmd.exe /s /c "<cmd>" as the shell
//    - Pipes for output capture, drained by a reader thread
// ============================================================

#ifdef _WIN32
#ifndef BIRUN_PROC_WIN32_HPP
#define BIRUN_PROC_WIN32_HPP

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace birun::proc {

constexpr int kDefaultGraceMs = 3000;
constexpr int kExitTimeout    = 124;
constexpr int kExitCancelled  = 130;

// ------------------------------------------------------------
//  Global state
// ------------------------------------------------------------
inline std::atomic<bool> g_shutdown{false};
inline std::atomic<bool> g_cancel{false};
inline std::atomic<int>  g_shutdownSig{0};

// All children belong to this job. TerminateJobObject kills
// the whole process tree atomically — even deeper grandchildren.
inline HANDLE g_job = INVALID_HANDLE_VALUE;
inline std::once_flag g_jobInit;

inline void ensureJob() {
    std::call_once(g_jobInit, [] {
        g_job = CreateJobObjectW(nullptr, nullptr);
        if (g_job == INVALID_HANDLE_VALUE) return;

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job,
            JobObjectExtendedLimitInformation, &info, sizeof info);
    });
}

// ------------------------------------------------------------
//  UTF-8 ⇄ UTF-16
// ------------------------------------------------------------
inline std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        (int)s.size(), w.data(), n);
    return w;
}

inline std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(),
                        (int)w.size(), s.data(), n,
                        nullptr, nullptr);
    return s;
}

// ------------------------------------------------------------
//  Console control handler (≈ SIGINT)
// ------------------------------------------------------------
inline BOOL WINAPI consoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_shutdownSig.exchange((int)type);
        g_shutdown.store(true, std::memory_order_release);
        if (g_job != INVALID_HANDLE_VALUE)
            TerminateJobObject(g_job, 130);
        return TRUE;
    }
    return FALSE;
}

inline void installSignalHandlers() {
    ensureJob();
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
}

inline bool shutdownRequested() {
    return g_shutdown.load(std::memory_order_acquire);
}
inline int shutdownSignal() {
    return g_shutdownSig.load(std::memory_order_relaxed);
}

// ------------------------------------------------------------
//  Internal cancel
// ------------------------------------------------------------
inline bool cancelRequested() {
    return g_cancel.load(std::memory_order_acquire);
}
inline void resetCancel() {
    g_cancel.store(false, std::memory_order_release);
}
inline void requestCancel() {
    g_cancel.store(true, std::memory_order_release);
    if (g_job != INVALID_HANDLE_VALUE)
        TerminateJobObject(g_job, kExitCancelled);
}

// ------------------------------------------------------------
//  Options / Result (identical to POSIX)
// ------------------------------------------------------------
struct RunOpts {
    int timeoutMs = 0;
    int graceMs   = kDefaultGraceMs;
    std::vector<std::pair<std::string, std::string>> env;
    std::string cwd;
};

struct RunResult {
    int    exitCode  = -1;
    bool   timedOut  = false;
    bool   cancelled = false;
    bool   signalled = false;
    int    signal    = 0;
    double seconds   = 0;
};

// ------------------------------------------------------------
//  Env block (KEY=VALUE\0 ... \0\0)
//  Empty return → pass nullptr → child inherits parent env.
// ------------------------------------------------------------
inline std::vector<wchar_t> buildEnvBlock(
    const std::vector<std::pair<std::string,std::string>>& extra)
{
    if (extra.empty()) return {};

    std::map<std::wstring, std::wstring> env;
    LPWCH parentEnv = GetEnvironmentStringsW();
    if (parentEnv) {
        for (LPWCH p = parentEnv; *p; ) {
            std::wstring entry = p;
            p += entry.size() + 1;
            auto eq = entry.find(L'=');
            if (eq == std::wstring::npos || eq == 0) continue;
            env[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
        FreeEnvironmentStringsW(parentEnv);
    }
    for (auto& kv : extra)
        env[toWide(kv.first)] = toWide(kv.second);

    std::vector<wchar_t> block;
    for (auto& kv : env) {
        std::wstring kvStr = kv.first + L"=" + kv.second;
        block.insert(block.end(), kvStr.begin(), kvStr.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

inline double elapsedMs(std::chrono::steady_clock::time_point s) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - s).count();
}

// ------------------------------------------------------------
//  Spawn one shell command
// ------------------------------------------------------------
struct Spawned {
    HANDLE hProcess = nullptr;
    DWORD  pid      = 0;
    HANDLE hReadOut = nullptr;
    bool   ok       = false;
};

inline Spawned spawnShell(const std::string& cmd,
                          const RunOpts& o,
                          bool capture, bool silent)
{
    Spawned sp;
    ensureJob();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (capture || silent) {
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return sp;
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    if (capture || silent) {
        si.hStdOutput = hWrite;
        si.hStdError  = hWrite;
    } else {
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    }
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring wcmd = L"cmd.exe /s /c \"" + toWide(cmd) + L"\"";
    std::wstring wcwd = toWide(o.cwd);
    std::vector<wchar_t> envBlock = buildEnvBlock(o.env);

    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED;

    BOOL ok = CreateProcessW(
        nullptr,
        wcmd.data(),
        nullptr, nullptr,
        TRUE,
        flags,
        envBlock.empty() ? nullptr : envBlock.data(),
        wcwd.empty() ? nullptr : wcwd.c_str(),
        &si, &pi);

    if (hWrite) CloseHandle(hWrite);

    if (!ok) {
        if (hRead) CloseHandle(hRead);
        return sp;
    }

    if (g_job != INVALID_HANDLE_VALUE)
        AssignProcessToJobObject(g_job, pi.hProcess);

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    sp.hProcess = pi.hProcess;
    sp.pid      = pi.dwProcessId;
    sp.hReadOut = hRead;
    sp.ok       = true;
    return sp;
}

// ------------------------------------------------------------
//  Wait + drain output
// ------------------------------------------------------------
inline RunResult waitFor(const Spawned& sp,
                         const RunOpts& o,
                         std::chrono::steady_clock::time_point start,
                         std::string* out)
{
    RunResult r;
    bool killed = false, timedOut = false, cancelled = false;
    auto graceEnd = std::chrono::steady_clock::time_point::max();

    std::thread reader;
    if (sp.hReadOut) {
        reader = std::thread([h = sp.hReadOut, out]() {
            char buf[4096];
            DWORD n = 0;
            while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0) {
                if (out) out->append(buf, (size_t)n);
            }
            CloseHandle(h);
        });
    }

    for (;;) {
        DWORD wr = WaitForSingleObject(sp.hProcess, 50);
        if (wr == WAIT_OBJECT_0) break;

        auto now = std::chrono::steady_clock::now();

        if (!killed) {
            bool want = false;
            if (o.timeoutMs > 0 &&
                elapsedMs(start) >= (double)o.timeoutMs) {
                timedOut = true; want = true;
            } else if (shutdownRequested()) {
                want = true;
            } else if (cancelRequested()) {
                cancelled = true; want = true;
            }
            if (want) {
                killed = true;
                graceEnd = now + std::chrono::milliseconds(o.graceMs);
                // Try a graceful Ctrl+Break first (acts like SIGINT).
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, sp.pid);
            }
        }
        if (killed && now >= graceEnd) {
            TerminateProcess(sp.hProcess,
                             timedOut ? kExitTimeout :
                             cancelled ? kExitCancelled : 130);
        }
    }

    if (reader.joinable()) reader.join();

    DWORD code = 0;
    GetExitCodeProcess(sp.hProcess, &code);
    CloseHandle(sp.hProcess);

    r.exitCode  = (int)code;
    r.timedOut  = timedOut;
    r.cancelled = cancelled;
    if (timedOut)           { r.exitCode = kExitTimeout; r.signalled = true; }
    else if (cancelled)     { r.exitCode = kExitCancelled; }
    else if (code >= 0xC0000000UL) {
        r.signalled = true;
        r.signal    = (int)(code & 0xFF);
        r.exitCode  = 128 + r.signal;
    }
    r.seconds = elapsedMs(start) / 1000.0;
    return r;
}

// ------------------------------------------------------------
//  Public API — matches POSIX exactly
// ------------------------------------------------------------
inline RunResult runLiveEx(const std::string& cmd, const RunOpts& o) {
    auto start = std::chrono::steady_clock::now();
    auto sp = spawnShell(cmd, o, /*capture=*/false, /*silent=*/false);
    if (!sp.ok) return {};
    return waitFor(sp, o, start, nullptr);
}
inline int runLive(const std::string& cmd) {
    return runLiveEx(cmd, {}).exitCode;
}

inline RunResult runCapturedEx(const std::string& cmd, std::string* out,
                               const RunOpts& o) {
    auto start = std::chrono::steady_clock::now();
    auto sp = spawnShell(cmd, o, /*capture=*/true, /*silent=*/false);
    if (!sp.ok) return {};
    return waitFor(sp, o, start, out);
}
inline int runCaptured(const std::string& cmd, std::string* out) {
    return runCapturedEx(cmd, out, {}).exitCode;
}

inline RunResult runSilentEx(const std::string& cmd, const RunOpts& o) {
    auto start = std::chrono::steady_clock::now();
    auto sp = spawnShell(cmd, o, /*capture=*/false, /*silent=*/true);
    if (!sp.ok) return {};
    return waitFor(sp, o, start, nullptr);
}
inline int runSilent(const std::string& cmd) {
    return runSilentEx(cmd, {}).exitCode;
}

// Windows simplification: merge stdout+stderr into `out`.
inline RunResult runSplitEx(const std::string& cmd,
                            std::string* out, std::string* err,
                            const RunOpts& o) {
    auto r = runCapturedEx(cmd, out, o);
    if (err) err->clear();
    return r;
}
inline int runSplit(const std::string& cmd, std::string* o, std::string* e) {
    return runSplitEx(cmd, o, e, {}).exitCode;
}

} // namespace birun::proc

#endif // BIRUN_PROC_WIN32_HPP
#endif // _WIN32