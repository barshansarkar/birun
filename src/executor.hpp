#pragma once
// ============================================================
//  birun · executor.hpp
// ============================================================

#include <map>
#include <set>
#include <string>
#include <vector>

namespace birun {

// ------------------------------------------------------------
//  Verbosity
// ------------------------------------------------------------
enum class Verbosity : int {
    Quiet   = 0,   // errors only
    Normal  = 1,   // headers, progress, success
    Verbose = 2,   // + plan, cache diagnostics, timings
};

struct Task {
    std::string              name;
    std::string              desc;
    std::vector<std::string> depends;
    std::vector<std::string> runs;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};

struct Options {
    bool        dryRun    = false;
    int         jobs      = 1;
    bool        list      = false;
    bool        watch     = false;
    bool        noCache   = false;
    bool        force     = false;   // ignore cache hits, still refresh entries
    bool        clean     = false;   // wipe cache file
    Verbosity   verbosity = Verbosity::Normal;
    std::string file      = "birun.bi";
    std::string task;
};

class Executor {
public:
    Executor(const std::map<std::string, Task>& tasks, const Options& opt);

    int run(const std::string& target);

    std::vector<std::string> plan(const std::string& target);

private:
    const std::map<std::string, Task>& tasks_;
    const Options&                     opt_;

    std::vector<std::string> order_;
    std::set<std::string>    visited_;
    std::set<std::string>    visiting_;

    void visit(const std::string& name);

    // ---- printing (verbosity-aware, non-static) ----
    void printHeader(const Task& t, int idx = 0, int total = 0) const;
    void printDone  (const Task& t, double secs) const;
    void printCached(const Task& t) const;
    void printFail  (const Task& t, int code) const;

    // Streams live to stdout when `captured == nullptr`.
    // Buffers stdout+stderr into `*captured` otherwise (parallel mode).
    int runCommands(const Task& t, std::string* captured = nullptr) const;

    int runSequential();
    int runParallel();
};

} // namespace birun
// ============================================================
//  END OF FILE
// ============================================================