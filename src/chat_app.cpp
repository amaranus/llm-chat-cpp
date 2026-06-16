#include "chat_app.h"
#include "utils.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <csignal>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include "readline_compat.h"

namespace fs = std::filesystem;

static std::atomic<bool> g_running{true};
static std::mutex g_print_mutex;

static void signal_handler(int) {
    g_running = false;
}

static std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::string detect_mime(const std::string& path) {
    std::string ext = to_lower(fs::path(path).extension().string());
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".json") return "application/json";
    if (ext == ".md" || ext == ".markdown") return "text/markdown";
    if (ext == ".txt" || ext == ".log") return "text/plain";
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") return "text/x-c++src";
    if (ext == ".c" || ext == ".h" || ext == ".hpp") return "text/x-csrc";
    if (ext == ".py") return "text/x-python";
    if (ext == ".js" || ext == ".ts") return "text/javascript";
    if (ext == ".rs") return "text/x-rust";
    if (ext == ".go") return "text/x-go";
    if (ext == ".java") return "text/x-java";
    if (ext == ".sh" || ext == ".bash") return "text/x-shellscript";
    if (ext == ".yaml" || ext == ".yml") return "text/yaml";
    if (ext == ".xml") return "text/xml";
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".csv") return "text/csv";
    if (ext == ".toml") return "text/plain";
    return "text/plain";
}

static bool is_image_mime(const std::string& mime) {
    return mime.find("image/") == 0;
}

static std::string base64_encode(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        unsigned int n = static_cast<unsigned char>(input[i]) << 16;
        if (i + 1 < input.size()) n |= static_cast<unsigned char>(input[i + 1]) << 8;
        if (i + 2 < input.size()) n |= static_cast<unsigned char>(input[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < input.size()) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < input.size()) ? table[n & 0x3F] : '=';
    }
    return result;
}

static std::string read_file_binary(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string read_file_text(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

ChatApp::ChatApp(std::string llm_url, std::string mcp_url, int max_context)
    : llm_url_(std::move(llm_url)), mcp_url_(std::move(mcp_url)), max_context_(max_context) {}

void ChatApp::add_file(const std::string& path) {
    fs::file_status status;
    try {
        status = fs::status(path);
    } catch (const fs::filesystem_error& e) {
        std::cout << utils::color("Error: ", 31) << e.what() << "\n";
        return;
    }

    if (!fs::exists(status)) {
        std::cout << utils::color("Error: ", 31) << "File not found: " << path << "\n";
        return;
    }

    if (fs::is_directory(status)) {
        std::cout << utils::color("Error: ", 31) << "Path is a directory: " << path << "\n";
        return;
    }

    std::string abs_path = fs::absolute(path).string();
    std::string filename = fs::path(path).filename().string();
    std::string mime = detect_mime(abs_path);
    bool is_image = is_image_mime(mime);

    std::string content;
    if (is_image) {
        content = base64_encode(read_file_binary(abs_path));
    } else {
        content = read_file_text(abs_path);
    }

    if (content.empty()) {
        std::cout << utils::color("Error: ", 31) << "Could not read file: " << path << "\n";
        return;
    }

    FileAttachment fa;
    fa.path = abs_path;
    fa.filename = filename;
    fa.content = content;
    fa.mime_type = mime;
    fa.is_image = is_image;

    files_.push_back(std::move(fa));

    std::cout << utils::color("File added: ", 32) << filename
              << " (" << mime;
    if (is_image) {
        std::cout << ", " << (content.size() * 100 / 1024 / 1024 + 1) << " KB encoded";
    } else {
        std::cout << ", " << content.size() << " bytes";
    }
    std::cout << ")\n";
}

void ChatApp::remove_file(const std::string& path) {
    std::string abs_path;
    try {
        abs_path = fs::absolute(path).string();
    } catch (...) {
        abs_path = path;
    }

    for (auto it = files_.begin(); it != files_.end(); ++it) {
        if (it->path == abs_path || it->filename == path) {
            std::cout << utils::color("File removed: ", 33) << it->filename << "\n";
            files_.erase(it);
            return;
        }
    }
    std::cout << utils::color("File not found in attachments: ", 33) << path << "\n";
}

void ChatApp::list_files() {
    if (files_.empty()) {
        std::cout << "No files attached.\n";
        return;
    }
    std::cout << "Attached files (" << files_.size() << "):\n";
    for (size_t i = 0; i < files_.size(); ++i) {
        const auto& f = files_[i];
        std::cout << "  " << (i + 1) << ". " << utils::color(f.filename, 36)
                  << " (" << f.mime_type;
        if (f.is_image) {
            std::cout << ", image";
        } else {
            std::cout << ", " << f.content.size() << " bytes";
        }
        std::cout << ")\n";
    }
}

void ChatApp::clear_files() {
    files_.clear();
    std::cout << "All files removed.\n";
}

llm::LLMClient::json ChatApp::build_user_message(const std::string& text) {
    if (files_.empty()) {
        return {{"role", "user"}, {"content", text}};
    }

    llm::LLMClient::json content = llm::LLMClient::json::array();

    for (const auto& f : files_) {
        if (f.is_image) {
            content.push_back({
                {"type", "image_url"},
                {"image_url", {
                    {"url", "data:" + f.mime_type + ";base64," + f.content}
                }}
            });
        } else {
            std::string file_block = "[" + f.filename + "]\n" + f.content;
            content.push_back({{"type", "text"}, {"text", file_block}});
        }
    }

    if (!text.empty()) {
        content.push_back({{"type", "text"}, {"text", text}});
    }

    return {{"role", "user"}, {"content", content}};
}

bool ChatApp::handle_command(const std::string& input, json& messages,
                             const std::vector<mcp::MCPTool>& tools,
                             llm::LLMClient& llm) {
    if (input == "/quit" || input == "/exit") {
        std::cout << "Bye.\n";
        return false;
    }
    if (input == "/help") {
        print_help();
        return true;
    }
    if (input == "/clear") {
        messages = llm::LLMClient::json::array();
        std::cout << "Chat history cleared.\n";
        return true;
    }
    if (input == "/tools") {
        if (tools.empty()) {
            std::cout << "No MCP tools available.\n";
        } else {
            std::cout << "MCP tools (" << tools.size() << "): ";
            for (size_t i = 0; i < tools.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << utils::color(tools[i].name, 36);
            }
            std::cout << "\n";
        }
        return true;
    }
    if (input == "/files") {
        list_files();
        return true;
    }
    if (input == "/clearfiles") {
        clear_files();
        return true;
    }
    if (input.substr(0, 6) == "/read ") {
        std::string path = utils::trim(input.substr(6));
        if (path.empty()) {
            std::cout << utils::color("Usage: ", 33) << "/read <file_path>\n";
        } else {
            add_file(path);
        }
        return true;
    }
    if (input.substr(0, 8) == "/remove ") {
        std::string path = utils::trim(input.substr(8));
        if (path.empty()) {
            std::cout << utils::color("Usage: ", 33) << "/remove <file_path>\n";
        } else {
            remove_file(path);
        }
        return true;
    }
    if (input == "/models") {
        auto models = llm.fetch_models_with_status();
        if (models.empty()) {
            std::cout << utils::color("No models found.\n", 33);
            return true;
        }
        std::cout << utils::color("Router Models:\n", 90);
        for (size_t i = 0; i < models.size(); ++i) {
            std::cout << "  " << utils::color(std::to_string(i + 1), 33) << ". " << models[i].id;
            if (models[i].max_context > 0) {
                std::cout << utils::color(" [ctx: " + std::to_string(models[i].max_context) + "]", 32);
            }
            if (models[i].status == "loaded") {
                std::cout << utils::color(" (loaded ✓)", 32);
                if (models[i].id == selected_model_) {
                    std::cout << utils::color(" ← current", 36);
                }
            } else if (models[i].status == "loading") {
                std::cout << utils::color(" (loading...)", 33);
            } else {
                std::cout << utils::color(" (unloaded)", 90);
            }
            std::cout << "\n";
        }
        std::cout << "  " << utils::color(std::to_string(models.size() + 1), 31) << ". "
                  << utils::color("Unload all models", 31) << "\n";
        std::cout << utils::color("Select [1-" + std::to_string(models.size() + 1)
                                  + "]: ", 36);
        std::cout.flush();

        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) {
            try {
                int idx = std::stoi(line);
                if (idx >= 1 && idx <= static_cast<int>(models.size())) {
                    selected_model_ = models[idx - 1].id;
                    int ctx = models[idx - 1].max_context;
                    if (ctx > 0) max_context_ = ctx;
                    std::cout << utils::color("Switched to: ", 32) << selected_model_
                              << utils::color(", context: ", 90) << max_context_ << "\n";
                } else if (idx == static_cast<int>(models.size() + 1)) {
                    for (const auto& m : models) {
                        if (m.status == "loaded") {
                            if (llm.unload_model(m.id)) {
                                std::cout << utils::color("Unloaded: ", 33) << m.id << "\n";
                            } else {
                                std::cout << utils::color("Failed: ", 31) << m.id << "\n";
                            }
                        }
                    }
                    selected_model_.clear();
                }
            } catch (...) {}
        }
        return true;
    }
    return true;
}

void ChatApp::run() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    print_logo();
    std::cout << "\n";

    http::HttpClient http;
    mcp::MCPClient mcp(mcp_url_, http);
    llm::LLMClient llm(llm_url_, http);
    llm.set_abort_check([&]() { return !g_running; });

    {
        auto info = llm.fetch_model_info();
        if (info.max_context > 0) {
            max_context_ = info.max_context;
        }
        std::cout << utils::color("Server: ", 90) << llm_url_ << "\n";

        auto models_with_status = llm.fetch_models_with_status();
        if (models_with_status.empty()) {
            selected_model_ = "default";
            std::cout << utils::color("Model: ", 90) << info.name
                      << utils::color(", context: ", 90) << max_context_ << "\n";
        } else {
            std::cout << utils::color("Models:\n", 90);
            for (size_t i = 0; i < models_with_status.size(); ++i) {
                const auto& m = models_with_status[i];
                int ctx = m.max_context > 0 ? m.max_context : max_context_;
                std::cout << "  " << utils::color(std::to_string(i + 1), 33)
                          << ". " << m.id;
                if (m.status == "loaded") {
                    std::cout << utils::color(" (loaded)", 32);
                }
                if (ctx > 0) {
                    std::cout << utils::color(" [ctx: " + std::to_string(ctx) + "]", 32);
                }
                std::cout << "\n";
            }
            std::cout << utils::color("Select model [1-"
                                      + std::to_string(models_with_status.size()) + "]: ", 36);
            std::cout.flush();

            std::string input;
            std::getline(std::cin, input);
            if (!input.empty()) {
                try {
                    int idx = std::stoi(input);
                    if (idx >= 1 && idx <= static_cast<int>(models_with_status.size())) {
                        selected_model_ = models_with_status[idx - 1].id;
                        int ctx = models_with_status[idx - 1].max_context;
                        if (ctx > 0) max_context_ = ctx;
                    }
                } catch (...) {}
            }

            if (selected_model_.empty()) {
                selected_model_ = models_with_status[0].id;
                int ctx = models_with_status[0].max_context;
                if (ctx > 0) max_context_ = ctx;
            }
            std::cout << utils::color("Using: ", 32) << selected_model_
                      << utils::color(", context: ", 90) << max_context_ << "\n";
        }
    }

    std::cout << "Connecting to MCP: " << mcp_url_ << " ... ";
    std::cout.flush();
    try {
        auto init_resp = mcp.initialize();
        std::string sid = mcp.session_id();
        std::cout << utils::color("ok", 32);
        if (!sid.empty()) {
            std::cout << " (session: " << sid.substr(0, 16) << "...)";
        }
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cout << utils::color("failed", 31) << " (" << e.what() << ")\n";
    }

    std::vector<mcp::MCPTool> tools;
    try {
        tools = mcp.list_tools();
        std::cout << "MCP tools (" << tools.size() << "): ";
        for (size_t i = 0; i < tools.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << utils::color(tools[i].name, 36);
        }
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cout << utils::color("Failed to list tools: ", 33) << e.what() << "\n";
    }

    std::cout << utils::color(std::string(utils::get_terminal_width(), '-'), 90) << "\n";
    print_help();
    std::cout << "\n";

    llm::LLMClient::json messages = llm::LLMClient::json::array();
    llm::LLMClient::json openai_tools = llm::LLMClient::json::array();

    for (const auto& t : tools) {
        llm::LLMClient::json ot = {
            {"type", "function"},
            {"function", {
                {"name", t.name},
                {"description", t.description},
                {"parameters", t.input_schema}
            }}
        };
        openai_tools.push_back(std::move(ot));
    }

    while (g_running) {
        char* line = readline(utils::rl_prompt(utils::color(utils::bold(">> "), 32)).c_str());
        if (!line) {
            std::cout << "\n";
            break;
        }

        std::string input(line);
        std::free(line);

        if (input.empty()) continue;
        add_history(input.c_str());

        if (input[0] == '/') {
            bool handled = handle_command(input, messages, tools, llm);
            if (handled) continue;
            break;
        }

        messages.push_back(build_user_message(input));

        bool tool_loop = true;
        int tool_call_count = 0;
        const int max_tool_calls = 20;

        while (tool_loop && g_running && tool_call_count < max_tool_calls) {
            std::string full_content;
            bool stream_started = false;
            bool is_reasoning = false;
            std::atomic<bool> thinking_done{false};
            std::thread thinking_thread;

            try {
                thinking_thread = std::thread([&]() {
                    const char spinner[] = "|/-\\";
                    int i = 0;
                    while (!thinking_done && g_running) {
                        {
                            std::lock_guard<std::mutex> lock(g_print_mutex);
                            std::cout << "\r" << utils::color(std::string(" ") + spinner[i % 4] + " Thinking...", 90) << std::flush;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(80));
                        i++;
                    }
                });

                auto result = llm.chat_stream(messages,
                    [&](const std::string& token, bool reasoning) {
                        if (!stream_started) {
                            stream_started = true;
                            thinking_done = true;
                            thinking_thread.join();
                            std::lock_guard<std::mutex> lock(g_print_mutex);
                            std::cout << "\r" << std::string(utils::get_terminal_width(), ' ') << "\r";
                            std::cout << utils::color(std::string(utils::get_terminal_width(), '-'), 90) << "\n";
                            std::cout.flush();

                            if (reasoning) {
                                is_reasoning = true;
                                std::cout << utils::color("── Thinking ──", 90) << "\n";
                                std::cout.flush();
                            } else {
                                std::cout << utils::color(utils::bold("Response: "), 36);
                                std::cout.flush();
                            }
                        }

                        if (reasoning && !is_reasoning) {
                            is_reasoning = true;
                            std::cout << "\n" << utils::color("── Thinking ──", 90) << "\n";
                            std::cout.flush();
                        } else if (!reasoning && is_reasoning) {
                            is_reasoning = false;
                            std::cout << "\n" << utils::color("── Response ──", 90) << "\n";
                            std::cout.flush();
                        }

                        if (reasoning) {
                            std::lock_guard<std::mutex> lock(g_print_mutex);
                            std::cout << utils::color(token, 90) << std::flush;
                        } else {
                            std::cout << token << std::flush;
                            full_content += token;
                        }
                    },
                    openai_tools,
                    selected_model_);

                if (!stream_started) {
                    thinking_done = true;
                    if (thinking_thread.joinable()) thinking_thread.join();
                    std::lock_guard<std::mutex> lock(g_print_mutex);
                    std::cout << "\r" << std::string(utils::get_terminal_width(), ' ') << "\r" << std::flush;
                }

                if (stream_started) {
                    std::cout << "\n";
                    std::cout << utils::color(std::string(utils::get_terminal_width(), '-'), 90) << "\n";
                }

                result.content = full_content;

                if (result.content.empty() && result.tool_calls.empty()) {
                    std::cout << utils::color("[Model returned empty response]", 33) << "\n";
                    break;
                }

                print_stats(result);

                if (!result.tool_calls.empty()) {
                    llm::LLMClient::json assistant_msg = {
                        {"role", "assistant"},
                        {"content", full_content.empty() ? llm::LLMClient::json() : llm::LLMClient::json(full_content)}
                    };
                    assistant_msg["tool_calls"] = result.tool_calls;
                    messages.push_back(assistant_msg);

                    for (const auto& tc : result.tool_calls) {
                        tool_call_count++;

                        auto& func = tc["function"];
                        std::string tool_name = func["name"];
                        llm::LLMClient::json tool_args = llm::LLMClient::json::parse(func["arguments"].get<std::string>());
                        std::string tool_call_id = tc["id"];

                        std::cout << utils::color("  ⚡ Tool call: ", 33)
                                  << utils::color(utils::bold(tool_name), 33) << "(";
                        bool first = true;
                        for (auto& [key, val] : tool_args.items()) {
                            if (!first) std::cout << ", ";
                            std::cout << key << "=" << val.dump();
                            first = false;
                        }
                        std::cout << ")\n";
                        std::cout.flush();

                        llm::LLMClient::json mcp_result;
                        try {
                            mcp_result = mcp.call_tool(tool_name, tool_args);
                        } catch (const std::exception& e) {
                            mcp_result = {
                                {"result", {
                                    {"content", llm::LLMClient::json::array({{{"type", "text"}, {"text", std::string("Error: ") + e.what()}}})}
                                }}
                            };
                        }

                        std::string tool_result_str;
                        if (mcp_result.contains("result") &&
                            mcp_result["result"].contains("content")) {
                            for (const auto& c : mcp_result["result"]["content"]) {
                                if (c.value("type", "") == "text") {
                                    if (!tool_result_str.empty()) tool_result_str += "\n";
                                    tool_result_str += c.value("text", "");
                                }
                            }
                        } else {
                            tool_result_str = mcp_result.dump(2);
                        }

                        std::cout << utils::color("  ← Result: ", 32)
                                  << tool_result_str << "\n\n";
                        std::cout.flush();

                        messages.push_back({
                            {"role", "tool"},
                            {"content", tool_result_str},
                            {"tool_call_id", tool_call_id}
                        });
                    }
                } else {
                    messages.push_back({{"role", "assistant"}, {"content", result.content}});
                    tool_loop = false;
                }

            } catch (const std::exception& e) {
                thinking_done = true;
                if (thinking_thread.joinable()) thinking_thread.join();
                std::lock_guard<std::mutex> lock(g_print_mutex);
                std::cout << "\r" << std::string(utils::get_terminal_width(), ' ') << "\r" << std::flush;
                std::cout << utils::color(utils::bold("ERROR: "), 31) << e.what() << "\n";
                break;
            }
        }

        if (tool_call_count >= max_tool_calls) {
            std::cout << utils::color("Maximum tool call count reached.", 33) << "\n";
        }
    }
}

void ChatApp::print_logo() {
    std::cout << R"(██      ██      ███    ███        ██████ ██   ██  █████  ████████  )" "\n";
    std::cout << R"(██      ██      ████  ████       ██      ██   ██ ██   ██    ██     )" "\n";
    std::cout << R"(██      ██      ██ ████ ██ █████ ██      ███████ ███████    ██     )" "\n";
    std::cout << R"(██      ██      ██  ██  ██       ██      ██   ██ ██   ██    ██     )" "\n";
    std::cout << R"(███████ ███████ ██      ██        ██████ ██   ██ ██   ██    ██     )" "\n";
}

void ChatApp::print_help() {
    std::cout << "Commands:\n";
    std::cout << "  " << utils::color("/quit", 33) << " or " << utils::color("/exit", 33) << " — Exit\n";
    std::cout << "  " << utils::color("/help", 33) << " — Show this help\n";
    std::cout << "  " << utils::color("/tools", 33) << " — List MCP tools\n";
    std::cout << "  " << utils::color("/clear", 33) << " — Clear chat history\n";
    std::cout << "  " << utils::color("/read <path>", 33) << " — Add file to context (text, image, pdf)\n";
    std::cout << "  " << utils::color("/files", 33) << " — List attached files\n";
    std::cout << "  " << utils::color("/remove <path>", 33) << " — Remove attached file\n";
    std::cout << "  " << utils::color("/clearfiles", 33) << " — Remove all attached files\n";
    std::cout << "  " << utils::color("/models", 33) << " — List / switch / unload models\n";
    std::cout << utils::color(std::string(utils::get_terminal_width(), '-'), 90) << "\n";
}

void ChatApp::print_stats(const llm::LLMClient::ChatResult& r) {
    std::ostringstream line;
    line << utils::color(" ─ ", 90);

    if (r.usage.completion_tokens > 0) {
        int total = r.usage.total_tokens > 0 ? r.usage.total_tokens : r.usage.completion_tokens;
        line << r.usage.prompt_tokens << " → " << r.usage.completion_tokens
             << " tokens - ";
        double ts = r.tokens_per_second > 0.0
            ? r.tokens_per_second
            : (r.duration_ms > 0 ? (r.usage.completion_tokens * 1000.0 / r.duration_ms) : 0.0);
        line << std::fixed << std::setprecision(1) << ts << " t/s - ";
        if (max_context_ > 0) {
            line << "%" << (total * 100 / max_context_)
                 << " context (" << total << "/" << max_context_ << ") - ";
        }
    }
    line << r.duration_ms << "ms";

    if (!r.model_name.empty()) {
        line << " - ";
        int cols = utils::get_terminal_width();
        std::string raw = line.str();
        size_t visible = 0;
        bool in_escape = false;
        for (char c : raw) {
            if (c == '\033') { in_escape = true; continue; }
            if (in_escape) { if (c == 'm') in_escape = false; continue; }
            visible++;
        }
        int pad = (cols > static_cast<int>(visible) + 2) ? cols - visible - r.model_name.size() - 1 : 2;
        for (int i = 0; i < pad; ++i) line << " ";
        line << r.model_name;
    }

    std::cout << line.str() << "\n";
}