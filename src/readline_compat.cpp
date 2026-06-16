#include "readline_compat.h"

#ifdef _WIN32

char* readline(const char* prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return nullptr;
    }
    char* result = static_cast<char*>(std::malloc(line.size() + 1));
    if (result) {
        std::strcpy(result, line.c_str());
    }
    return result;
}

void add_history(const char*) {
}

#endif
