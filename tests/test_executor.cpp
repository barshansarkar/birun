#include <catch2/catch_test_macros.hpp>

#include "executor.hpp"

#include <algorithm>
#include <map>
#include <string>

using namespace birun;

// ------------------------------------------------------------
//  Fixture: a small dependency graph
//    clean ─→ build ─→ test ─┐
//                            ├─→ all
//              docs  ────────┘
// ------------------------------------------------------------
static std::map<std::string, Task> makeTasks() {
    std::map<std::string, Task> t;

    Task clean{"clean", "wipe", {}, {"true"}};
    Task build{"build", "compile", {"clean"}, {"true"}};
    Task test{"test", "run tests", {"build"}, {"true"}};
    Task docs{"docs", "build docs", {}, {"true"}};
    Task all{"all", "everything", {"test", "docs"}, {}};

    t["clean"] = clean;
    t["build"] = build;
    t["test"]  = test;
    t["docs"]  = docs;
    t["all"]   = all;
    return t;
}

// ============================================================
//  plan()
// ============================================================
TEST_CASE("Executor: plan returns every task exactly once", "[executor]") {
    auto tasks = makeTasks();
    Options opt;
    Executor ex(tasks, opt);

    auto order = ex.plan("all");
    REQUIRE(order.size() == 5);

    std::vector<std::string> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted == std::vector<std::string>{
        "all", "build", "clean", "docs", "test"});
}

TEST_CASE("Executor: plan respects dependency order", "[executor]") {
    auto tasks = makeTasks();
    Options opt;
    Executor ex(tasks, opt);

    auto order = ex.plan("all");
    auto idx = [&](const std::string& n) {
        return std::find(order.begin(), order.end(), n) - order.begin();
    };

    CHECK(idx("clean") < idx("build"));
    CHECK(idx("build") < idx("test"));
    CHECK(idx("test")  < idx("all"));
    CHECK(idx("docs")  < idx("all"));
}

TEST_CASE("Executor: unknown task throws", "[executor]") {
    auto tasks = makeTasks();
    Options opt;
    Executor ex(tasks, opt);

    CHECK_THROWS_AS(ex.plan("nope"), std::runtime_error);
}

TEST_CASE("Executor: cycle is detected", "[executor]") {
    std::map<std::string, Task> t;
    t["a"] = Task{"a", "", {"b"}, {}};
    t["b"] = Task{"b", "", {"a"}, {}};

    Options opt;
    Executor ex(t, opt);

    CHECK_THROWS_AS(ex.plan("a"), std::runtime_error);
}

TEST_CASE("Executor: missing dependency throws", "[executor]") {
    std::map<std::string, Task> t;
    t["a"] = Task{"a", "", {"ghost"}, {}};

    Options opt;
    Executor ex(t, opt);

    CHECK_THROWS_AS(ex.plan("a"), std::runtime_error);
}

// ============================================================
//  run()
// ============================================================
TEST_CASE("Executor: dry-run does not execute the command", "[executor]") {
    std::map<std::string, Task> t;
    // If this ran for real, it would exit 42.
    t["boom"] = Task{"boom", "", {}, {"exit 42"}};

    Options opt;
    opt.dryRun = true;

    Executor ex(t, opt);
    CHECK(ex.run("boom") == 0);
}

TEST_CASE("Executor: real run propagates exit code", "[executor]") {
    std::map<std::string, Task> t;
    t["boom"] = Task{"boom", "", {}, {"exit 7"}};

    Options opt;
    opt.jobs = 1;

    Executor ex(t, opt);
    CHECK(ex.run("boom") == 7);
}

TEST_CASE("Executor: sequential success", "[executor]") {
    auto tasks = makeTasks();
    Options opt;
    opt.jobs = 1;

    Executor ex(tasks, opt);
    CHECK(ex.run("all") == 0);
}

TEST_CASE("Executor: parallel success", "[executor]") {
    auto tasks = makeTasks();
    Options opt;
    opt.jobs = 4;

    Executor ex(tasks, opt);
    CHECK(ex.run("all") == 0);
}

TEST_CASE("Executor: parallel stops on first failure", "[executor]") {
    std::map<std::string, Task> t;
    t["ok"]   = Task{"ok",   "", {},      {"true"}};
    t["boom"] = Task{"boom", "", {"ok"},  {"exit 3"}};
    t["end"]  = Task{"end",  "", {"boom"},{"true"}};

    Options opt;
    opt.jobs = 2;

    Executor ex(t, opt);
    CHECK(ex.run("end") == 3);
}