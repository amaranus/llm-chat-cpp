#pragma once

#include <string>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32

char* readline(const char* prompt);
void add_history(const char* line);

#else

#include <readline/readline.h>
#include <readline/history.h>

#endif
