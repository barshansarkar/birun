// ============================================================
//  birun · tests/test_integration.cpp
//  End-to-end CLI tests: spawn the real birun binary, run it
//  against temporary config files, check exit codes + output.
// ============================================================
#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef BIRUN_BINARY_PATH
#  error "BIRUN_BINARY_PATH must be defined by CMake"
#endif

namespace fs = std::filesystem;

// ------------------------------------------------------------
//  Spawn birun with args, capture stdout+stderr, wait for exit.
// ------------------------------------------------------------
struct CliResult {
    int         exitCode = -1;
    std::string output;
};

static CliResult runCli(const std::vector<std::string>& args,
                        const std::string& cwd = {})
{
    int pfd[2];
    if (::pipe(pfd) < 0) return {};

    pid_t pid = ::fork();
    if (pid < 0) { ::close(pfd[0]); ::close(pfd[1]); return {}; }

    if (pid == 0) {
        ::close(pfd[0]);
        ::dup2(pfd[1], STDOUT_FILENO);
        ::dup2(pfd[1], STDERR_FILENO);
        ::close(pfd[1]);

        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(126);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(BIRUN_BINARY_PATH));
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        ::execv(BIRUN_BINARY_PATH, argv.data());
        ::_exit(127);
    }

    ::close(pfd[1]);
    CliResult r;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(pfd[0], buf, sizeof buf);
        if (n > 0) r.output.append(buf, (size_t)n);
        else if (n == 0) break;
        else if (errno != EINTR) break;
    }
    ::close(pfd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status))   r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exitCode = 128 + WTERMSIG(status);
    return r;
}

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------
static std::string mkTempDir(const std::string& tag) {
    auto base = fs::temp_directory_path() /
                ("birun-cli-" + tag + "-" + std::to_string(::getpid()));
    fs::create_directories(base);
    return base.string();
}

static void writeFile(const std::string& p, const std::string& s) {
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream f(p);
    f << s;
}

// ============================================================
//  Tests
// ============================================================
TEST_CASE("CLI: --version prints version", "[integration]") {
    auto r = runCli({"--version"});
    CHECK(r.exitCode == 0);
    CHECK(r.output.find("birun") != std::string::npos);
    CHECK(r.output.find("1.") != std::string::npos);
}

TEST_CASE("CLI: --help exits 0 and mentions usage", "[integration]") {
    auto r = runCli({"--help"});
    CHECK(r.exitCode == 0);
    CHECK(r.output.find("USAGE") != std::string::npos);
}

TEST_CASE("CLI: unknown option exits non-zero", "[integration]") {
    auto r = runCli({"--definitely-not-a-flag"});
    CHECK(r.exitCode != 0);
    CHECK(r.output.find("unknown option") != std::string::npos);
}

TEST_CASE("CLI: --init creates a starter config", "[integration]") {
    auto dir  = mkTempDir("init");
    auto path = dir + "/birun.bi";

    auto r = runCli({"--init", "-f", path});
    CHECK(r.exitCode == 0);
    CHECK(fs::exists(path));

    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    CHECK(ss.str().find("route TASK") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CLI: list shows tasks from config", "[integration]") {
    auto dir = mkTempDir("list");
    auto cfg = dir + "/birun.bi";
    writeFile(cfg,
        "route TASK \"/hello\" {\n"
        "    desc(\"say hello\")\n"
        "    run(\"true\")\n"
        "}\n"
        "route TASK \"/world\" {\n"
        "    desc(\"say world\")\n"
        "    run(\"true\")\n"
        "}\n");

    auto r = runCli({"-f", cfg, "-l"});
    CHECK(r.exitCode == 0);
    CHECK(r.output.find("hello") != std::string::npos);
    CHECK(r.output.find("world") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CLI: run a simple task", "[integration]") {
    auto dir = mkTempDir("run");
    auto cfg = dir + "/birun.bi";
    writeFile(cfg,
        "route TASK \"/ok\" {\n"
        "    run(\"echo hi-from-cli\")\n"
        "}\n");

    auto r = runCli({"-f", cfg, "ok"}, dir);
    CHECK(r.exitCode == 0);
    CHECK(r.output.find("hi-from-cli") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CLI: failing task propagates exit code", "[integration]") {
    auto dir = mkTempDir("fail");
    auto cfg = dir + "/birun.bi";
    writeFile(cfg,
        "route TASK \"/boom\" {\n"
        "    run(\"exit 7\")\n"
        "}\n");

    auto r = runCli({"-f", cfg, "boom"}, dir);
    CHECK(r.exitCode == 7);

    fs::remove_all(dir);
}

TEST_CASE("CLI: --dry-run never executes", "[integration]") {
    auto dir = mkTempDir("dry");
    auto cfg = dir + "/birun.bi";
    auto sentinel = dir + "/touched";
    writeFile(cfg,
        "route TASK \"/touch\" {\n"
        "    run(\"touch " + sentinel + "\")\n"
        "}\n");

    auto r = runCli({"-f", cfg, "-n", "touch"}, dir);
    CHECK(r.exitCode == 0);
    CHECK_FALSE(fs::exists(sentinel));

    fs::remove_all(dir);
}

TEST_CASE("CLI: parallel failure cancels siblings", "[integration]") {
    auto dir = mkTempDir("parcancel");
    auto cfg = dir + "/birun.bi";
    auto slowMarker = dir + "/slow-done";
    // One task sleeps for 5s then writes a marker (should NOT complete).
    // Another fails immediately.
    writeFile(cfg,
        "route TASK \"/slow\" {\n"
        "    run(\"sleep 5 && touch " + slowMarker + "\")\n"
        "}\n"
        "route TASK \"/boom\" {\n"
        "    run(\"sleep 0.2 && exit 9\")\n"
        "}\n"
        "route TASK \"/all\" {\n"
        "    depends(\"slow\", \"boom\")\n"
        "}\n");

    auto t0 = std::chrono::steady_clock::now();
    auto r  = runCli({"-f", cfg, "-j", "4", "all"}, dir);
    auto dt = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(r.exitCode == 9);
    CHECK_FALSE(fs::exists(slowMarker));
    // Should not have waited for the full 5 s sleep.
    CHECK(dt < 4.0);

    fs::remove_all(dir);
}

TEST_CASE("CLI: cache invalidates on config change", "[integration]") {
    auto dir = mkTempDir("cache");
    auto cfg = dir + "/birun.bi";
    auto src = dir + "/src.txt";
    writeFile(src, "v1");
    writeFile(cfg,
        "route TASK \"/build\" {\n"
        "    inputs(\"" + src + "\")\n"
        "    outputs(\"" + dir + "/out\")\n"
        "    run(\"cat " + src + " > " + dir + "/out\")\n"
        "}\n");

    // First run — cache miss.
    auto r1 = runCli({"-f", cfg, "build"}, dir);
    CHECK(r1.exitCode == 0);

    // Second run — cache hit (verify by changing input then re-running).
    writeFile(src, "v2");
    auto r2 = runCli({"-f", cfg, "build"}, dir);
    CHECK(r2.exitCode == 0);

    std::ifstream f(dir + "/out");
    std::stringstream ss; ss << f.rdbuf();
    CHECK(ss.str().find("v2") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CLI: --json emits parseable graph", "[integration]") {
    auto dir = mkTempDir("json");
    auto cfg = dir + "/birun.bi";
    writeFile(cfg,
        "route TASK \"/a\" { run(\"true\") }\n"
        "route TASK \"/b\" { depends(\"a\") run(\"true\") }\n");

    auto r = runCli({"-f", cfg, "--json"});
    CHECK(r.exitCode == 0);
    CHECK(r.output.find("\"tasks\"") != std::string::npos);
    CHECK(r.output.find("\"name\": \"a\"") != std::string::npos);
    CHECK(r.output.find("\"name\": \"b\"") != std::string::npos);

    fs::remove_all(dir);
}