#pragma once
#include "task.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace birun {

namespace color {
    inline bool enabled = false;
    inline const char* RST() { return enabled ? "\033[0m"  : ""; }
    inline const char* BLD() { return enabled ? "\033[1m"  : ""; }
    inline const char* DIM() { return enabled ? "\033[2m"  : ""; }
    inline const char* RED() { return enabled ? "\033[31m" : ""; }
    inline const char* GRN() { return enabled ? "\033[32m" : ""; }
    inline const char* CYN() { return enabled ? "\033[36m" : ""; }
}

class Executor {
public:
    explicit Executor(const TaskMap& tasks) : tasks_(tasks) {}

    int run(const std::string& name) {
        if (!tasks_.count(name))
            throw std::runtime_error("unknown task '" + name + "'");

        order_.clear();
        visited_.clear();
        visiting_.clear();
        visit(name);

        for (const auto& n : order_) {
            const Task& t = tasks_.at(n);

            std::cout << color::BLD() << color::CYN() << "> " << n
                      << color::RST();
            if (!t.desc.empty())
                std::cout << "  " << color::DIM() << t.desc << color::RST();
            std::cout << "\n";
            std::cout.flush();

            if (t.runs.empty()) {
                std::cout << color::DIM() << "  (no commands)\n"
                          << color::RST();
            }

            for (const auto& cmd : t.runs) {
                std::cout << color::DIM() << "  $ " << color::RST()
                          << cmd << "\n";
                std::cout.flush();

                int rc = std::system(cmd.c_str());
                if (rc != 0) {
                    int code = (rc != -1 && WIFEXITED(rc))
                                   ? WEXITSTATUS(rc) : 1;
                    std::cerr << color::RED() << "x task '" << n
                              << "' failed (exit " << code << ")\n"
                              << color::RST();
                    return code ? code : 1;
                }
            }
        }
        return 0;
    }

private:
    const TaskMap&           tasks_;
    std::vector<std::string> order_;
    std::set<std::string>    visited_;
    std::set<std::string>    visiting_;

    void visit(const std::string& name) {
        if (visited_.count(name)) return;
        if (visiting_.count(name))
            throw std::runtime_error(
                "dependency cycle detected at task '" + name + "'");

        auto it = tasks_.find(name);
        if (it == tasks_.end())
            throw std::runtime_error(
                "task '" + name + "' not found (referenced as dependency)");

        visiting_.insert(name);
        for (const auto& d : it->second.depends) visit(d);
        visiting_.erase(name);
        visited_.insert(name);
        order_.push_back(name);
    }
};

} // namespace birun
