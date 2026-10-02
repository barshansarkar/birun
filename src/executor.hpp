#pragma once
// ============================================================
//  birun · executor.hpp  (v1.0.0)
// ============================================================
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace birun {

enum class Verbosity : int { Quiet = 0, Normal = 1, Verbose = 2 };

struct Task {
    std::string              name;
    std::string              desc;
    std::vector<std::string> depends;
    std::vector<std::string> runs;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;

    std::vector<std::pair<std::string, std::string>> env;
    std::string              cwd;
    int                      timeoutSec     = 0;
    int                      retryCount     = 0;
    int                      retryBackoffMs = 1000;
};

struct Options {
    bool        dryRun    = false;
    int         jobs      = 1;
    bool        list      = false;
    bool        watch     = false;
    bool        noCache   = false;
    bool        force     = false;
    bool        clean     = false;
    bool        silent    = false;
    bool        stats     = false;
    bool        explain   = false;
    int         graceSec  = 3;
    Verbosity   verbosity = Verbosity::Normal;
    std::string file      = "birun.bi";
    std::string task;
};

struct TaskStat {
    std::string name;
    double      seconds   = 0;
    int         attempts  = 1;
    int         exitCode  = 0;
    bool        cached    = false;
};

struct Stats {
    std::vector<TaskStat> tasks;
    int    hits       = 0;
    int    misses     = 0;
    double totalSec   = 0;
};

class Executor {
public:
    Executor(const std::map<std::string, Task>& tasks, const Options& opt);

    int run(const std::string& target);
    std::vector<std::string> plan(const std::string& target);

    const Stats& stats() const { return stats_; }

private:
    const std::map<std::string, Task>& tasks_;
    const Options&                     opt_;

    std::vector<std::string> order_;
    std::set<std::string>    visited_;
    std::set<std::string>    visiting_;

    Stats stats_;
    mutable std::mutex        statsMtx_;
    mutable std::mutex        outMtx_;

    void visit(const std::string& name);

    void printHeader (const Task& t, int idx = 0, int total = 0) const;
    void printDone   (const Task& t, double secs) const;
    void printCached (const Task& t) const;
    void printFail   (const Task& t, int code) const;
    void printRetry  (const Task& t, int attempt, int max,
                      double waitSec) const;
    void explain     (const std::string& name, const std::string& why) const;
    void printStats  () const;

    int runCommands(const Task& t, std::string* captured = nullptr) const;

    int runSequential();
    int runParallel();
};

} // namespace birun