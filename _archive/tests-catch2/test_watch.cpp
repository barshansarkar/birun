#include <catch2/catch_test_macros.hpp>

#include "watch.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace birun::watch;

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------
static std::string makeTempDir(const std::string& tag) {
    auto base = fs::temp_directory_path() /
                ("birun-test-" + tag + "-" + std::to_string(::getpid()));
    fs::create_directories(base);
    return base.string();
}

static void writeFile(const std::string& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

// ============================================================
//  Filtering
// ============================================================
TEST_CASE("Watch: ignores noisy directories", "[watch]") {
    CHECK(isIgnored("/home/x/proj/.git/config"));
    CHECK(isIgnored("/home/x/proj/node_modules/foo.js"));
    CHECK(isIgnored("/home/x/proj/build/out.o"));
    CHECK(isIgnored("/home/x/proj/target/debug/app"));
    CHECK(isIgnored("/home/x/proj/__pycache__/x.pyc"));
    CHECK_FALSE(isIgnored("/home/x/proj/src/main.cpp"));
    CHECK_FALSE(isIgnored("/home/x/proj/birun.bi"));
}

TEST_CASE("Watch: watches source extensions", "[watch]") {
    CHECK(isWatchedExt("main.cpp"));
    CHECK(isWatchedExt("script.py"));
    CHECK(isWatchedExt("lib.rs"));
    CHECK(isWatchedExt("birun.bi"));
    CHECK(isWatchedExt("package.json"));
    CHECK(isWatchedExt("Cargo.toml"));
    CHECK_FALSE(isWatchedExt("image.png"));
    CHECK_FALSE(isWatchedExt("binary.bin"));
    CHECK_FALSE(isWatchedExt("archive.zip"));
}

// ============================================================
//  Snapshot + diff
// ============================================================
TEST_CASE("Watch: snapshot captures files", "[watch]") {
    auto dir = makeTempDir("snap");
    writeFile(dir + "/a.cpp", "int a = 1;");
    writeFile(dir + "/b.py",  "b = 2");
    writeFile(dir + "/skip.png", "binary");

    auto snap = snapshot({dir});
    CHECK(snap.count(dir + "/a.cpp") == 1);
    CHECK(snap.count(dir + "/b.py")  == 1);
    CHECK(snap.count(dir + "/skip.png") == 0);   // not a watched ext

    fs::remove_all(dir);
}

TEST_CASE("Watch: diff detects changed file", "[watch]") {
    auto dir = makeTempDir("diff");
    auto path = dir + "/x.cpp";
    writeFile(path, "v1");

    auto s1 = snapshot({dir});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeFile(path, "v2");

    auto s2 = snapshot({dir});
    auto ch = diff(s1, s2);
    REQUIRE(ch.size() == 1);
    CHECK(ch[0] == path);

    fs::remove_all(dir);
}

TEST_CASE("Watch: diff detects added file", "[watch]") {
    auto dir = makeTempDir("add");
    writeFile(dir + "/base.cpp", "x");

    auto s1 = snapshot({dir});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeFile(dir + "/new.cpp", "y");

    auto s2 = snapshot({dir});
    auto ch = diff(s1, s2);
    REQUIRE(ch.size() == 1);
    CHECK(ch[0] == dir + "/new.cpp");

    fs::remove_all(dir);
}

TEST_CASE("Watch: diff detects removed file", "[watch]") {
    auto dir = makeTempDir("rm");
    auto path = dir + "/gone.cpp";
    writeFile(path, "x");

    auto s1 = snapshot({dir});
    fs::remove(path);
    auto s2 = snapshot({dir});

    auto ch = diff(s1, s2);
    REQUIRE(ch.size() == 1);
    CHECK(ch[0] == path);

    fs::remove_all(dir);
}

TEST_CASE("Watch: diff is empty when nothing changed", "[watch]") {
    auto dir = makeTempDir("noop");
    writeFile(dir + "/stable.cpp", "content");

    auto s1 = snapshot({dir});
    auto s2 = snapshot({dir});
    CHECK(diff(s1, s2).empty());

    fs::remove_all(dir);
}