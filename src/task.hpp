#pragma once
#include <map>
#include <string>
#include <vector>

namespace birun {

struct Task {
    std::string              name;
    std::string              desc;
    std::vector<std::string> depends;
    std::vector<std::string> runs;
};

using TaskMap = std::map<std::string, Task>;

} // namespace birun
