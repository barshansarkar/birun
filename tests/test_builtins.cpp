#include <catch2/catch_test_macros.hpp>

#include "bi/interpreter.hpp"
#include "bi/value.hpp"
#include "builtins_birun.hpp"

using namespace bi;

// Run a single bi expression with birun builtins registered.
static Value evalBirun(const std::string& expr) {
    Interpreter interp;
    birun::registerBirunBuiltins(interp.globals());
    auto [hasVal, v] = interp.runSourceRepl(expr, "test.bi");
    REQUIRE(hasVal);
    return v;
}

// ============================================================
//  which / exists
// ============================================================
TEST_CASE("Builtins: which finds sh", "[builtins]") {
    Value v = evalBirun(R"(which("sh"))");
    CHECK(v.type == Value::STR);
    CHECK_FALSE(v.strView().empty());
}

TEST_CASE("Builtins: which returns null for missing tool", "[builtins]") {
    Value v = evalBirun(R"(which("this-tool-does-not-exist-xyz123"))");
    CHECK(v.type == Value::NIL);
}

TEST_CASE("Builtins: exists /etc/passwd", "[builtins]") {
    Value v = evalBirun(R"(exists("/etc/passwd"))");
    CHECK(v.type == Value::BOOL);
    CHECK(v.boolean);
}

TEST_CASE("Builtins: exists missing path returns false", "[builtins]") {
    Value v = evalBirun(R"(exists("/nope/nope/nope"))");
    CHECK(v.type == Value::BOOL);
    CHECK_FALSE(v.boolean);
}

// ============================================================
//  sh / shStatus / shFull
// ============================================================
TEST_CASE("Builtins: sh captures stdout", "[builtins]") {
    Value v = evalBirun(R"(sh("echo hello-world"))");
    CHECK(v.type == Value::STR);
    CHECK(v.strView() == "hello-world");
}

TEST_CASE("Builtins: sh trims trailing newline", "[builtins]") {
    Value v = evalBirun(R"(sh("printf 'x\n\n'"))");
    CHECK(v.strView() == "x");
}

TEST_CASE("Builtins: sh throws on non-zero exit", "[builtins]") {
    CHECK_THROWS_AS(
        evalBirun(R"(sh("exit 5"))"),
        std::runtime_error);
}

TEST_CASE("Builtins: sh with strict=false ignores exit code", "[builtins]") {
    Value v = evalBirun(R"(sh("echo hi; exit 5", false))");
    CHECK(v.type == Value::STR);
    CHECK(v.strView() == "hi");
}

TEST_CASE("Builtins: shStatus returns exit code", "[builtins]") {
    Value v = evalBirun(R"(shStatus("exit 3"))");
    CHECK(v.type == Value::INT);
    CHECK(v.i == 3);
}

TEST_CASE("Builtins: shStatus zero on success", "[builtins]") {
    Value v = evalBirun(R"(shStatus("true"))");
    CHECK(v.i == 0);
}

TEST_CASE("Builtins: shFull returns map with code/out/err", "[builtins]") {
    Value v = evalBirun(R"(shFull("echo out; echo err >&2"))");
    REQUIRE(v.type == Value::MAP);
    auto& m = *v.mapPtr();
    CHECK(toInt(m["code"]) == 0);
    CHECK(m["out"].strView() == "out");
    CHECK(m["err"].strView() == "err");
}

// ============================================================
//  Host info
// ============================================================
TEST_CASE("Builtins: os returns a known value", "[builtins]") {
    Value v = evalBirun("os()");
    REQUIRE(v.type == Value::STR);
    auto s = std::string(v.strView());
    CHECK((s == "linux" || s == "darwin" ||
           s == "windows" || s == "unknown"));
}

TEST_CASE("Builtins: arch returns a known value", "[builtins]") {
    Value v = evalBirun("arch()");
    REQUIRE(v.type == Value::STR);
    auto s = std::string(v.strView());
    CHECK((s == "x86_64" || s == "arm64" || s == "x86" ||
           s == "arm"    || s == "unknown"));
}

TEST_CASE("Builtins: cwd returns non-empty string", "[builtins]") {
    Value v = evalBirun("cwd()");
    REQUIRE(v.type == Value::STR);
    CHECK_FALSE(v.strView().empty());
}

TEST_CASE("Builtins: inCI returns bool", "[builtins]") {
    Value v = evalBirun("inCI()");
    CHECK(v.type == Value::BOOL);
}

// ============================================================
//  glob
// ============================================================
TEST_CASE("Builtins: glob on /etc/*.conf returns array", "[builtins]") {
    Value v = evalBirun(R"(glob("/etc/*.conf"))");
    CHECK(v.type == Value::ARR);
    // Note: /etc may or may not have *.conf files; just ensure no crash.
}

// ============================================================
//  Logic integration
// ============================================================
TEST_CASE("Builtins: logic with env() and if", "[builtins]") {
    // Uses a function so the value is returned, not just logged.
    Interpreter interp;
    birun::registerBirunBuiltins(interp.globals());
    interp.runSource(R"(
        fn pick() {
            let x = env("BIRUN_TEST_VAR", "fallback")
            if (x == "fallback") { return "ok" }
            return "nope"
        }
        let result = pick()
    )", "test.bi");

    Value* p = interp.globals()->find("result");
    REQUIRE(p != nullptr);
    CHECK(p->type == Value::STR);
    CHECK(p->strView() == "ok");
}