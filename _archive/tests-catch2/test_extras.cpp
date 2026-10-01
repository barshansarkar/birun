#include <catch2/catch_test_macros.hpp>

#include "extras.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace birun;
using namespace birun::extras;

// ---- JSON escape ----
TEST_CASE("Extras: jsonEscape handles quotes and newlines", "[extras]") {
    CHECK(jsonEscape("hi") == "hi");
    CHECK(jsonEscape("a\"b") == "a\\\"b");
    CHECK(jsonEscape("a\nb") == "a\\nb");
    CHECK(jsonEscape("a\\b") == "a\\\\b");
}

// ---- JSON output ----
TEST_CASE("Extras: printJson outputs valid structure", "[extras]") {
    std::map<std::string, Task> t;
    t["a"] = Task{"a", "desc-a", {}, {"true"}};
    t["b"] = Task{"b", "desc-b", {"a"}, {"echo hi"}};

    std::vector<std::string> order = {"a", "b"};

    // Redirect stdout
    std::stringstream buf;
    auto* old = std::cout.rdbuf(buf.rdbuf());
    printJson(t, order, "birun.bi", "0.5.0");
    std::cout.rdbuf(old);

    std::string out = buf.str();
    CHECK(out.find("\"version\": \"0.5.0\"")   != std::string::npos);
    CHECK(out.find("\"name\": \"a\"")          != std::string::npos);
    CHECK(out.find("\"name\": \"b\"")          != std::string::npos);
    CHECK(out.find("\"depends\": [\"a\"]")     != std::string::npos);
    CHECK(out.find("\"echo hi\"")              != std::string::npos);
}

// ---- Graphviz ----
TEST_CASE("Extras: printGraph emits DOT edges", "[extras]") {
    std::map<std::string, Task> t;
    t["build"] = Task{"build", "compile", {}, {"true"}};
    t["test"]  = Task{"test",  "run",     {"build"}, {"true"}};

    std::vector<std::string> order = {"build", "test"};

    std::stringstream buf;
    auto* old = std::cout.rdbuf(buf.rdbuf());
    printGraph(t, order, "birun.bi");
    std::cout.rdbuf(old);

    std::string out = buf.str();
    CHECK(out.find("digraph birun")     != std::string::npos);
    CHECK(out.find("\"build\"")         != std::string::npos);
    CHECK(out.find("\"build\" -> \"test\"") != std::string::npos);
}

// ---- Shell completion ----
TEST_CASE("Extras: bash completion mentions tasks", "[extras]") {
    std::vector<std::string> tasks = {"build", "test", "clean"};

    std::stringstream buf;
    auto* old = std::cout.rdbuf(buf.rdbuf());
    printCompletion("bash", tasks);
    std::cout.rdbuf(old);

    std::string out = buf.str();
    CHECK(out.find("_birun_completions") != std::string::npos);
    CHECK(out.find("build") != std::string::npos);
    CHECK(out.find("test")  != std::string::npos);
}

TEST_CASE("Extras: zsh completion has _arguments", "[extras]") {
    std::vector<std::string> tasks = {"build"};

    std::stringstream buf;
    auto* old = std::cout.rdbuf(buf.rdbuf());
    printCompletion("zsh", tasks);
    std::cout.rdbuf(old);

    CHECK(buf.str().find("_arguments") != std::string::npos);
}

TEST_CASE("Extras: fish completion has complete -c", "[extras]") {
    std::vector<std::string> tasks = {"build"};

    std::stringstream buf;
    auto* old = std::cout.rdbuf(buf.rdbuf());
    printCompletion("fish", tasks);
    std::cout.rdbuf(old);

    CHECK(buf.str().find("complete -c birun") != std::string::npos);
}

// ---- --init ----
TEST_CASE("Extras: writeStarter creates file", "[extras]") {
    auto dir = std::filesystem::temp_directory_path() /
               ("birun-init-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto path = (dir / "birun.bi").string();

    CHECK(writeStarter(path, false));

    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    auto content = ss.str();

    CHECK(content.find("route TASK") != std::string::npos);
    CHECK(content.find("/hello")     != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Extras: writeStarter refuses overwrite without force", "[extras]") {
    auto dir = std::filesystem::temp_directory_path() /
               ("birun-init-test2-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto path = (dir / "birun.bi").string();

    CHECK(writeStarter(path, false));    // first time ok
    CHECK_FALSE(writeStarter(path, false)); // second time refused
    CHECK(writeStarter(path, true));     // force ok

    std::filesystem::remove_all(dir);
}