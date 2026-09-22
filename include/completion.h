#pragma once

#include <string>
#include <vector>

namespace completion {

const std::vector<std::string>& get_commands();
std::vector<std::string> match_commands(const std::string& prefix);

void setup();

#ifdef _WIN32
std::vector<std::string> get_completions(const std::string& input);
#endif

} // namespace completion
