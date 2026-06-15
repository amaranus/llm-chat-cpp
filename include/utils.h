#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace utils {

std::string color(const std::string& text, int code);
std::string bold(const std::string& text);
std::string rl_prompt(const std::string& text);
std::string trim(const std::string& s);
int get_terminal_width();
std::vector<std::string> split_lines(const std::string& s);
std::string env_or(const char* key, const std::string& fallback);
int env_int(const char* key, int fallback);

} // namespace utils