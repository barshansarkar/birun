#include <catch2/catch_test_macros.hpp>

#include "cache.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace birun::cache;

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------
static std::string makeTempDir(const std::string& tag) {
    auto base = fs::temp_directory_path() /
                ("birun-cache-" + tag + "-" + std::to_string(::getpid()));
    fs::create_directories(base);
    return base.string();
}

static void writeFile(const std::string& p, const std::string& content) {
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream f(p);
    f << content;
}

// ============================================================
//  Wildcard
// ============================================================
TEST_CASE("Cache: wildcardMatch basic", "[cache]") {
    CHECK(wildcardMatch("main.cpp",   "*.cpp"));
    CHECK(wildcardMatch("a.cpp",      "*.cpp"));
    CHECK_FALSE(wildcardMatch("a.h",  "*.cpp"));
    CHECK(wildcardMatch("main.cpp",   "main.*"));
    CHECK(wildcardMatch("main.cpp",   "m*.cpp"));
    CHECK(wildcardMatch("main.cpp",   "ma?n.cpp"));
    CHECK_FALSE(wildcardMatch("main.cpp", "ma?n.h"));
    CHECK(wildcardMatch("anything",   "*"));
}

// ============================================================
//  Glob expansion
// ============================================================
TEST_CASE("Cache: expandGlob one level", "[cache]") {
    auto dir = makeTempDir("glob1");
    writeFile(dir + "/a.cpp", "1");
    writeFile(dir + "/b.cpp", "2");
    writeFile(dir + "/c.h",   "3");

    auto out = expandGlob(dir + "/*.cpp");
    REQUIRE(out.size() == 2);

    fs::remove_all(dir);
}

TEST_CASE("Cache: expandGlob recursive", "[cache]") {
    auto dir = makeTempDir("glob2");
    writeFile(dir + "/src/a.cpp", "1");
    writeFile(dir + "/src/sub/b.cpp", "2");
    writeFile(dir + "/src/sub/deep/c.cpp", "3");
    writeFile(dir + "/src/x.h", "4");

    auto out = expandGlob(dir + "/**/*.cpp");
    REQUIRE(out.size() == 3);

    fs::remove_all(dir);
}

TEST_CASE("Cache: expandGlob literal path", "[cache]") {
    auto dir = makeTempDir("glob3");
    auto path = dir + "/single.txt";
    writeFile(path, "hello");

    auto out = expandGlob(path);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == path);

    fs::remove_all(dir);
}

// ============================================================
//  Hashing
// ============================================================
TEST_CASE("Cache: hashInputs deterministic", "[cache]") {
    auto dir = makeTempDir("hash1");
    writeFile(dir + "/a.cpp", "int x = 1;");
    writeFile(dir + "/b.cpp", "int y = 2;");

    auto h1 = hashInputs({dir + "/*.cpp"});
    auto h2 = hashInputs({dir + "/*.cpp"});
    CHECK(h1 == h2);
    CHECK_FALSE(h1.empty());

    fs::remove_all(dir);
}

TEST_CASE("Cache: hashInputs changes when content changes", "[cache]") {
    auto dir = makeTempDir("hash2");
    writeFile(dir + "/a.cpp", "v1");

    auto h1 = hashInputs({dir + "/*.cpp"});

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writeFile(dir + "/a.cpp", "v2");

    auto h2 = hashInputs({dir + "/*.cpp"});
    CHECK(h1 != h2);

    fs::remove_all(dir);
}

// ============================================================
//  outputsExist
// ============================================================
TEST_CASE("Cache: outputsExist true when file present", "[cache]") {
    auto dir = makeTempDir("out1");
    auto path = dir + "/build/app";
    writeFile(path, "binary");

    CHECK(outputsExist({path}));

    fs::remove_all(dir);
}

TEST_CASE("Cache: outputsExist false when missing", "[cache]") {
    auto dir = makeTempDir("out2");
    CHECK_FALSE(outputsExist({dir + "/missing.bin"}));
    fs::remove_all(dir);
}

TEST_CASE("Cache: outputsExist empty is true", "[cache]") {
    CHECK(outputsExist({}));
}

// ============================================================
//  CacheStore
// ============================================================
TEST_CASE("Cache: CacheStore round-trip", "[cache]") {
    auto dir = makeTempDir("store");
    auto path = dir + "/cache";

   CacheStore s;
    s.entries["build"] = "abcdef0123456789";
    s.entries["test"]  = "fedcba9876543210";
    s.save(path);

    auto loaded = CacheStore::load(path);
        REQUIRE(loaded.entries.size() == 2);
    CHECK(loaded.entries["build"] == "abcdef0123456789");
    CHECK(loaded.entries["test"]  == "fedcba9876543210");

    fs::remove_all(dir);
}

TEST_CASE("Cache: CacheStore missing file is empty", "[cache]") {
        auto loaded = CacheStore::load("/nope/nope/nope");
    CHECK(loaded.entries.empty());
}