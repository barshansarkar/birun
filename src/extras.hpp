#pragma once
// ============================================================
//  birun · extras.hpp
//  JSON output, Graphviz export, shell completions, --init.
// ============================================================

#include "bi/value.hpp"
#include "executor.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace birun::extras {

// ------------------------------------------------------------
//  JSON escaping
// ------------------------------------------------------------
inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

// ------------------------------------------------------------
//  JSON output
// ------------------------------------------------------------
inline void printJson(const std::map<std::string, Task>& tasks,
                      const std::vector<std::string>& order,
                      const std::string& file,
                      const std::string& version) {
    std::cout << "{\n";
    std::cout << "  \"version\": \"" << jsonEscape(version) << "\",\n";
    std::cout << "  \"file\": \""    << jsonEscape(file)    << "\",\n";
    std::cout << "  \"tasks\": [\n";

    for (size_t i = 0; i < order.size(); i++) {
        const Task& t = tasks.at(order[i]);
        std::cout << "    {\n";
        std::cout << "      \"name\": \"" << jsonEscape(t.name) << "\",\n";
        std::cout << "      \"desc\": \"" << jsonEscape(t.desc) << "\",\n";

        std::cout << "      \"depends\": [";
        for (size_t j = 0; j < t.depends.size(); j++) {
            if (j) std::cout << ", ";
            std::cout << "\"" << jsonEscape(t.depends[j]) << "\"";
        }
        std::cout << "],\n";

        std::cout << "      \"runs\": [";
        for (size_t j = 0; j < t.runs.size(); j++) {
            if (j) std::cout << ", ";
            std::cout << "\"" << jsonEscape(t.runs[j]) << "\"";
        }
        std::cout << "]\n";

        std::cout << "    }";
        if (i + 1 < order.size()) std::cout << ",";
        std::cout << "\n";
    }

    std::cout << "  ]\n";
    std::cout << "}\n";
}




// ------------------------------------------------------------
//  Plan JSON — one target's execution order
// ------------------------------------------------------------
inline void printPlanJson(const std::map<std::string, Task>& tasks,
                          const std::vector<std::string>& order,
                          const std::string& target,
                          const std::string& file,
                          int jobs) {
    std::cout << "{\n";
    std::cout << "  \"target\": \"" << jsonEscape(target) << "\",\n";
    std::cout << "  \"file\": \""   << jsonEscape(file)   << "\",\n";
    std::cout << "  \"jobs\": "     << jobs                << ",\n";
    std::cout << "  \"count\": "    << order.size()        << ",\n";
    std::cout << "  \"steps\": [\n";

    for (size_t i = 0; i < order.size(); i++) {
        const Task& t = tasks.at(order[i]);
        std::cout << "    {\n";
        std::cout << "      \"name\": \"" << jsonEscape(t.name) << "\",\n";
        std::cout << "      \"desc\": \"" << jsonEscape(t.desc) << "\",\n";

        std::cout << "      \"depends\": [";
        for (size_t j = 0; j < t.depends.size(); j++) {
            if (j) std::cout << ", ";
            std::cout << "\"" << jsonEscape(t.depends[j]) << "\"";
        }
        std::cout << "],\n";

        std::cout << "      \"runs\": [";
        for (size_t j = 0; j < t.runs.size(); j++) {
            if (j) std::cout << ", ";
            std::cout << "\"" << jsonEscape(t.runs[j]) << "\"";
        }
        std::cout << "]\n";

        std::cout << "    }";
        if (i + 1 < order.size()) std::cout << ",";
        std::cout << "\n";
    }

    std::cout << "  ]\n";
    std::cout << "}\n";
}
// ------------------------------------------------------------
//  Graphviz DOT output
// ------------------------------------------------------------
inline void printGraph(const std::map<std::string, Task>& tasks,
                       const std::vector<std::string>& order,
                       const std::string& file) {
    auto escape = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };

    std::cout << "digraph birun {\n";
    std::cout << "  rankdir=LR;\n";
    std::cout << "  graph [label=\"" << escape(file)
              << "\", labelloc=t, fontname=\"Helvetica\"];\n";
    std::cout << "  node  [shape=box, style=\"rounded,filled\", "
              << "fillcolor=\"#1a1b26\", fontcolor=\"#c0caf5\", "
              << "fontname=\"Helvetica\"];\n";
    std::cout << "  edge  [color=\"#565f89\"];\n\n";

    for (const auto& n : order) {
        const Task& t = tasks.at(n);
        std::cout << "  \"" << escape(n) << "\"";
        if (!t.desc.empty())
            std::cout << " [tooltip=\"" << escape(t.desc) << "\"]";
        std::cout << ";\n";
    }
    std::cout << "\n";

    for (const auto& n : order) {
        const Task& t = tasks.at(n);
        for (const auto& d : t.depends)
            std::cout << "  \"" << escape(d) << "\" -> \""
                      << escape(n) << "\";\n";
    }

    std::cout << "}\n";
}

// ------------------------------------------------------------
//  Shell completion
// ------------------------------------------------------------
inline void printCompletion(const std::string& shell,
                            const std::vector<std::string>& tasks) {
    if (shell == "bash") {
        std::cout <<
"# birun bash completion\n"
"# Install: birun --completion bash | sudo tee /etc/bash_completion.d/birun\n"
"\n"
"_birun_completions() {\n"
"    local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
"    local prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
"    local opts=\"--list -l --dry-run -n --jobs -j --watch -w --file -f "
"--json --graph --completion --init --help -h --version -v\"\n";
        std::cout << "    local tasks=\"";
        for (const auto& t : tasks) std::cout << t << " ";
        std::cout << "\"\n\n"
"    case \"$prev\" in\n"
"        -j|--jobs)  return 0 ;;\n"
"        -f|--file)  COMPREPLY=($(compgen -f -- \"$cur\")); return 0 ;;\n"
"        --completion) COMPREPLY=($(compgen -W \"bash zsh fish\" -- \"$cur\")); return 0 ;;\n"
"    esac\n"
"\n"
"    if [[ \"$cur\" == -* ]]; then\n"
"        COMPREPLY=($(compgen -W \"$opts\" -- \"$cur\"))\n"
"    else\n"
"        COMPREPLY=($(compgen -W \"$tasks\" -- \"$cur\"))\n"
"    fi\n"
"}\n"
"complete -F _birun_completions birun\n";
        return;
    }

    if (shell == "zsh") {
        std::cout <<
"#compdef birun\n"
"# birun zsh completion\n"
"# Install: birun --completion zsh > ~/.zsh/completions/_birun\n"
"\n"
"_birun() {\n"
"    local -a tasks\n";
        std::cout << "    tasks=(";
        for (const auto& t : tasks) std::cout << " '" << t << "'";
        std::cout << ")\n\n"
"_arguments -s \\\n"
"        '(-l --list)'{-l,--list}'[List tasks]' \\\n"
"        '(-n --dry-run)'{-n,--dry-run}'[Show plan only]' \\\n"
"        '(-w --watch)'{-w,--watch}'[Re-run on changes]' \\\n"
"        '(-j --jobs)'{-j,--jobs}'[Parallel jobs]:N:' \\\n"
"        '(-f --file)'{-f,--file}'[Config file]:file:_files' \\\n"
"        '--json[Print task graph as JSON]' \\\n"
"        '--graph[Print Graphviz DOT]' \\\n"
"        '--completion[Print shell completion]:shell:(bash zsh fish)' \\\n"
"        '--init[Create a starter birun.bi]' \\\n"
"        '(-h --help)'{-h,--help}'[Show help]' \\\n"
"        '(-v --version)'{-v,--version}'[Print version]' \\\n"
"        '1:task:->tasks' \\\n"
"        && return 0\n"
"\n"
"    _describe 'tasks' tasks\n"
"}\n"
"\n"
"_birun \"$@\"\n";
        return;
    }

    if (shell == "fish") {
        std::cout <<
"# birun fish completion\n"
"# Install: birun --completion fish > ~/.config/fish/completions/birun.fish\n"
"\n"
"complete -c birun -s l -l list       -d 'List tasks'\n"
"complete -c birun -s n -l dry-run    -d 'Show plan only'\n"
"complete -c birun -s w -l watch      -d 'Re-run on changes'\n"
"complete -c birun -s j -l jobs       -x -d 'Parallel jobs'\n"
"complete -c birun -s f -l file       -r -d 'Config file'\n"
"complete -c birun      -l json       -d 'Task graph as JSON'\n"
"complete -c birun      -l graph      -d 'Task graph as Graphviz'\n"
"complete -c birun      -l completion -x -a 'bash zsh fish'\n"
"complete -c birun      -l init       -d 'Create starter birun.bi'\n"
"complete -c birun -s h -l help       -d 'Show help'\n"
"complete -c birun -s v -l version    -d 'Print version'\n";
        for (const auto& t : tasks)
            std::cout << "complete -c birun -a '" << t
                      << "' -d 'birun task'\n";
        return;
    }

    std::cerr << "birun: unknown shell '" << shell
              << "' (try bash, zsh, or fish)\n";
}

// ------------------------------------------------------------
//  --init: starter config
// ------------------------------------------------------------
inline bool writeStarter(const std::string& path, bool force) {
    std::ifstream check(path);
    if (check.good() && !force) {
        std::cerr << "birun: '" << path
                  << "' already exists (use --init --force to overwrite)\n";
        return false;
    }
    check.close();

    std::ofstream f(path);
    if (!f) {
        std::cerr << "birun: cannot write '" << path << "'\n";
        return false;
    }

    f << "// birun.bi - birun task runner config\n"
         "// Docs: https://github.com/barshansarkar/bi\n"
         "//\n"
         "// Tasks are declared with `route TASK \"/name\" { ... }`.\n"
         "// The whole bi language is available: let, if, for, fn, env(), ...\n"
         "\n"
         "let mgr  = pkg()          // auto-detect: npm / cargo / pip / go / ...\n"
         "let isCI = inCI()\n"
         "\n"
         "route TASK \"/hello\" {\n"
         "    desc(\"Say hello\")\n"
         "    run(\"echo 'hello from birun'\")\n"
         "}\n"
         "\n"
         "route TASK \"/build\" {\n"
         "    desc(\"Build the project\")\n"
         "    depends(\"hello\")\n"
         "    if (mgr == \"cargo\") {\n"
         "        run(\"cargo build --release\")\n"
         "    } else if (mgr == \"pnpm\" || mgr == \"npm\" || mgr == \"yarn\") {\n"
         "        run(\"npm run build\")\n"
         "    } else {\n"
         "        run(\"echo 'no build system detected'\")\n"
         "    }\n"
         "}\n"
         "\n"
         "route TASK \"/test\" {\n"
         "    desc(\"Run the test suite\")\n"
         "    depends(\"build\")\n"
         "    run(\"echo 'running tests...'\")\n"
         "}\n"
         "\n"
         "route TASK \"/clean\" {\n"
         "    desc(\"Remove build artifacts\")\n"
         "    run(\"rm -rf build dist target\")\n"
         "}\n";

    f.close();
    std::cout << "created " << path << "\n";
    return true;
}

} // namespace birun::extras
// ============================================================
//  END OF FILE
// ============================================================