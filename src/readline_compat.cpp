#include "readline_compat.h"

#ifdef _WIN32

#include <windows.h>
#include <vector>
#include <algorithm>
#include "completion.h"

static std::string s_current_line;
static size_t s_cursor_pos = 0;

static size_t visible_length(const std::string& s) {
    size_t len = 0;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c == '\001' || c == '\033') {
            if (c == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
                i += 2;
                while (i < s.size() && s[i] >= '0' && s[i] <= ';') i++;
                if (i < s.size()) i++;
            } else {
                while (i < s.size() && s[i] != '\002' && s[i] != 'm') i++;
                if (i < s.size()) i++;
            }
            continue;
        }
        if (c == '\002') continue;
        len++;
    }
    return len;
}

static size_t utf8_char_count(const std::string& s, size_t byte_pos) {
    size_t count = 0;
    for (size_t i = 0; i < byte_pos && i < s.size(); i++) {
        unsigned char c = s[i];
        if ((c & 0xC0) != 0x80) count++;
    }
    return count;
}

static std::string wchar_to_utf8(wchar_t wc) {
    std::string result;
    if (wc < 0x80) {
        result += static_cast<char>(wc);
    } else if (wc < 0x800) {
        result += static_cast<char>(0xC0 | (wc >> 6));
        result += static_cast<char>(0x80 | (wc & 0x3F));
    } else {
        result += static_cast<char>(0xE0 | (wc >> 12));
        result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (wc & 0x3F));
    }
    return result;
}

static void redraw_line(const std::string& prompt) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    SHORT y = csbi.dwCursorPosition.Y;
    COORD start = {0, y};
    DWORD written;
    FillConsoleOutputCharacterA(h, ' ', csbi.dwSize.X, start, &written);
    SetConsoleCursorPosition(h, start);
    std::cout << prompt << s_current_line << std::flush;
    size_t vis = visible_length(prompt);
    size_t char_pos = utf8_char_count(s_current_line, s_cursor_pos);
    COORD pos = {static_cast<SHORT>(vis + char_pos), y};
    SetConsoleCursorPosition(h, pos);
}

char* readline(const char* prompt) {
    s_current_line.clear();
    s_cursor_pos = 0;
    std::cout << prompt << std::flush;

    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD prev_mode;
    GetConsoleMode(h_in, &prev_mode);
    SetConsoleMode(h_in, prev_mode | ENABLE_PROCESSED_INPUT);

    while (true) {
        INPUT_RECORD ir;
        DWORD events_read;

        if (!ReadConsoleInputW(h_in, &ir, 1, &events_read)) {
            return nullptr;
        }

        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) {
            continue;
        }

        KEY_EVENT_RECORD ke = ir.Event.KeyEvent;
        wchar_t wc = ke.uChar.UnicodeChar;
        WORD vk = ke.wVirtualKeyCode;
        DWORD ctrl = ke.dwControlKeyState;

        if (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
            if (wc == L'c' || wc == L'C') {
                std::cout << "\n";
                return nullptr;
            }
            if (wc == L'a') {
                s_cursor_pos = 0;
                redraw_line(prompt);
                continue;
            }
            if (wc == L'e') {
                s_cursor_pos = s_current_line.size();
                redraw_line(prompt);
                continue;
            }
        }

        if (wc == L'\t') {
            auto matches = completion::get_completions(s_current_line);
            if (matches.size() == 1) {
                size_t last_space = s_current_line.find_last_of(' ');
                std::string before;
                if (last_space != std::string::npos) {
                    before = s_current_line.substr(0, last_space + 1);
                }
                s_current_line = before + matches[0];
                s_cursor_pos = s_current_line.size();
                redraw_line(prompt);
            } else if (matches.size() > 1) {
                std::string common = matches[0];
                for (const auto& m : matches) {
                    size_t i = 0;
                    while (i < common.size() && i < m.size() && common[i] == m[i]) {
                        i++;
                    }
                    common.resize(i);
                }
                if (!common.empty()) {
                    size_t last_space = s_current_line.find_last_of(' ');
                    std::string before;
                    if (last_space != std::string::npos) {
                        before = s_current_line.substr(0, last_space + 1);
                    }
                    s_current_line = before + common;
                    s_cursor_pos = s_current_line.size();
                    redraw_line(prompt);
                }
                std::cout << "\n";
                for (const auto& m : matches) {
                    std::cout << "  " << m << "\n";
                }
                std::cout << prompt << s_current_line << std::flush;
            }
            continue;
        }

        if (wc == L'\r' || wc == L'\n') {
            std::cout << "\n";
            SetConsoleMode(h_in, prev_mode);
            char* result = static_cast<char*>(std::malloc(s_current_line.size() + 1));
            if (result) {
                std::strcpy(result, s_current_line.c_str());
            }
            return result;
        }

        if (wc == L'\b' || vk == VK_BACK) {
            if (s_cursor_pos > 0 && !s_current_line.empty()) {
                size_t pos = s_cursor_pos;
                while (pos > 0 && (static_cast<unsigned char>(s_current_line[pos - 1]) & 0xC0) == 0x80) {
                    pos--;
                }
                if (pos > 0) pos--;
                s_current_line.erase(pos, s_cursor_pos - pos);
                s_cursor_pos = pos;
                redraw_line(prompt);
            }
            continue;
        }

        if (vk == VK_DELETE) {
            if (s_cursor_pos < s_current_line.size()) {
                size_t end = s_cursor_pos + 1;
                while (end < s_current_line.size() &&
                       (static_cast<unsigned char>(s_current_line[end]) & 0xC0) == 0x80) {
                    end++;
                }
                s_current_line.erase(s_cursor_pos, end - s_cursor_pos);
                redraw_line(prompt);
            }
            continue;
        }

        if (vk == VK_LEFT) {
            if (s_cursor_pos > 0) {
                size_t pos = s_cursor_pos;
                while (pos > 0 && (static_cast<unsigned char>(s_current_line[pos - 1]) & 0xC0) == 0x80) {
                    pos--;
                }
                if (pos > 0) pos--;
                s_cursor_pos = pos;
                redraw_line(prompt);
            }
            continue;
        }

        if (vk == VK_RIGHT) {
            if (s_cursor_pos < s_current_line.size()) {
                s_cursor_pos++;
                while (s_cursor_pos < s_current_line.size() &&
                       (static_cast<unsigned char>(s_current_line[s_cursor_pos]) & 0xC0) == 0x80) {
                    s_cursor_pos++;
                }
                redraw_line(prompt);
            }
            continue;
        }

        if (vk == VK_HOME) {
            s_cursor_pos = 0;
            redraw_line(prompt);
            continue;
        }

        if (vk == VK_END) {
            s_cursor_pos = s_current_line.size();
            redraw_line(prompt);
            continue;
        }

        if (wc >= 32 && wc != 0xFFFF) {
            std::string utf8 = wchar_to_utf8(wc);
            s_current_line.insert(s_cursor_pos, utf8);
            s_cursor_pos += utf8.size();
            redraw_line(prompt);
        }
    }
}

void add_history(const char*) {
}

#else

#include <readline/readline.h>
#include <readline/history.h>

#endif
