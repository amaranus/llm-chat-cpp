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
#include <readline/readline.h>
#include <readline/history.h>

static std::atomic<bool> g_running{true};
static std::mutex g_print_mutex;

static void signal_handler(int) {
    g_running = false;
}

ChatApp::ChatApp(std::string llm_url, std::string mcp_url, int max_context)
    : llm_url_(std::move(llm_url)), mcp_url_(std::move(mcp_url)), max_context_(max_context) {}

void ChatApp::run() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    print_logo();
    std::cout << "\n";

    http::HttpClient http;
    mcp::MCPClient mcp(mcp_url_, http);
    llm::LLMClient llm(llm_url_, http);

    {
        auto info = llm.fetch_model_info();
        if (info.max_context > 0) {
            max_context_ = info.max_context;
            std::cout << utils::color("Model: ", 90) << info.name
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

        if (input == "/quit" || input == "/exit") {
            std::cout << "Bye.\n";
            break;
        }
        if (input == "/help") {
            print_help();
            continue;
        }
        if (input == "/clear") {
            messages = llm::LLMClient::json::array();
            std::cout << "Chat history cleared.\n";
            continue;
        }
        if (input == "/tools") {
            std::cout << "MCP tools (" << tools.size() << "): ";
            for (size_t i = 0; i < tools.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << utils::color(tools[i].name, 36);
            }
            std::cout << "\n";
            continue;
        }

        messages.push_back({{"role", "user"}, {"content", input}});

        bool tool_loop = true;
        int tool_call_count = 0;
        const int max_tool_calls = 20;

        while (tool_loop && g_running && tool_call_count < max_tool_calls) {
            try {
                std::string full_content;
                bool first_token = true;

                std::atomic<bool> thinking_done{false};
                std::thread thinking_thread([&]() {
                    const char spinner[] = "|/-\\";
                    int i = 0;
                    while (!thinking_done) {
                        {
                            std::lock_guard<std::mutex> lock(g_print_mutex);
                            std::cout << "\r" << utils::color(std::string(" ") + spinner[i % 4] + " Thinking...", 90) << std::flush;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(80));
                        i++;
                    }
                });

                auto result = llm.chat_stream(messages,
                    [&](const std::string& token) {
                        if (first_token) {
                            thinking_done = true;
                            thinking_thread.join();
                            std::lock_guard<std::mutex> lock(g_print_mutex);
                            std::cout << "\r" << std::string(utils::get_terminal_width(), ' ') << "\r";
                            std::cout << utils::color(std::string(utils::get_terminal_width(), '-'), 90) << "\n";
                            std::cout << utils::color(utils::bold("Response: "), 36);
                            std::cout.flush();
                            first_token = false;
                        }
                        std::cout << token << std::flush;
                        full_content += token;
                    },
                    openai_tools);

                if (first_token) {
                    thinking_done = true;
                    if (thinking_thread.joinable()) thinking_thread.join();
                    std::lock_guard<std::mutex> lock(g_print_mutex);
                    std::cout << "\r" << std::string(utils::get_terminal_width(), ' ') << "\r" << std::flush;
                }

                if (!first_token) {
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
    std::cout << "  " << utils::color("/clear", 33) << " — Clear chat history\n";
    std::cout << "  " << utils::color("/tools", 33) << " — List MCP tools\n";
    std::cout << utils::color(std::string(utils::get_terminal_width(), '-'), 90) << "\n";
}

void ChatApp::print_stats(const llm::LLMClient::ChatResult& r) {
    std::ostringstream line;
    line << utils::color(" ─ ", 90);

    if (r.usage.completion_tokens > 0) {
        int total = r.usage.total_tokens > 0 ? r.usage.total_tokens : r.usage.completion_tokens;
        line << r.usage.prompt_tokens << " → " << r.usage.completion_tokens
             << " tokens - ";
        double ts = r.duration_ms > 0
            ? (r.usage.completion_tokens * 1000.0 / r.duration_ms) : 0.0;
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