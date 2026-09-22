#include "completion.h"
#include <algorithm>

#ifdef _WIN32
#include "readline_compat.h"
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace completion {

static const std::vector<std::string> commands = {
    "/quit",
    "/exit",
    "/help",
    "/clear",
    "/tools",
    "/mcp",
    "/mcp connect",
    "/mcp disconnect",
    "/mcp status",
    "/read",
    "/files",
    "/remove",
    "/clearfiles",
    "/models"
};

const std::vector<std::string>& get_commands() {
    return commands;
}

std::vector<std::string> match_commands(const std::string& prefix) {
    std::vector<std::string> matches;
    for (const auto& cmd : commands) {
        if (cmd.size() >= prefix.size() &&
            cmd.compare(0, prefix.size(), prefix) == 0) {
            matches.push_back(cmd);
        }
    }
    return matches;
}

#ifndef _WIN32

static char* command_generator(const char* text, int state) {
    static size_t list_index;
    static std::string prefix;

    if (state == 0) {
        prefix = text;
        list_index = 0;
    }

    while (list_index < commands.size()) {
        const auto& cmd = commands[list_index++];
        if (prefix.empty() || cmd.compare(0, prefix.size(), prefix) == 0) {
            return strdup(cmd.c_str());
        }
    }
    return nullptr;
}

static char** command_completion(const char* text, int start, int) {
    (void)start;
    return rl_completion_matches(text, command_generator);
}

void setup() {
    rl_attempted_completion_function = command_completion;
    rl_completion_entry_function = nullptr;
    rl_completion_suppress_append = 1;
}

#else

static std::vector<std::string> s_matches;
static size_t s_match_index = 0;

void setup() {
}

std::vector<std::string> get_completions(const std::string& input) {
    std::string prefix;
    size_t last_space = input.find_last_of(' ');
    if (last_space != std::string::npos) {
        prefix = input.substr(last_space + 1);
    } else {
        prefix = input;
    }
    return match_commands(prefix);
}

bool has_completions(const std::string& input) {
    return !get_completions(input).empty();
}

std::string complete(const std::string& input) {
    auto matches = get_completions(input);
    if (matches.empty()) return input;

    size_t last_space = input.find_last_of(' ');
    std::string before;
    if (last_space != std::string::npos) {
        before = input.substr(0, last_space + 1);
    }

    if (matches.size() == 1) {
        return before + matches[0] + " ";
    }

    std::string common = matches[0];
    for (const auto& m : matches) {
        size_t i = 0;
        while (i < common.size() && i < m.size() && common[i] == m[i]) {
            i++;
        }
        common.resize(i);
    }

    if (common.size() > 0) {
        return before + common;
    }

    return input;
}

#endif

} // namespace completion
