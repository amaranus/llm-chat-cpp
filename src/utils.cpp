#include "utils.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace utils {

std::string color(const std::string& text, int code) {
    return "\033[" + std::to_string(code) + "m" + text + "\033[0m";
}

std::string bold(const std::string& text) {
    return "\033[1m" + text + "\033[0m";
}

std::string rl_prompt(const std::string& text) {
    std::string result;
    bool in_escape = false;
    for (char c : text) {
        if (c == '\033') {
            result += "\001";
            in_escape = true;
        }
        result += c;
        if (in_escape && c == 'm') {
            result += "\002";
            in_escape = false;
        }
    }
    return result;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string env_or(const char* key, const std::string& fallback) {
    const char* val = std::getenv(key);
    return val ? std::string(val) : fallback;
}

int env_int(const char* key, int fallback) {
    const char* val = std::getenv(key);
    if (!val) return fallback;
    try { return std::stoi(val); } catch (...) { return fallback; }
}

} // namespace utils