#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <csignal>
#include <functional>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cstdlib>
#include <sys/ioctl.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <readline/readline.h>
#include <readline/history.h>

using json = nlohmann::json;

static std::atomic<bool> g_running{true};
static std::mutex g_print_mutex;

static void signal_handler(int) {
    g_running = false;
}

namespace {

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

// ============================================================
// HTTP Client (libcurl wrapper)
// ============================================================
class HttpClient {
public:
    HttpClient() {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~HttpClient() {
        curl_global_cleanup();
    }

    struct Result {
        json body;
        std::vector<std::string> response_headers;
        long http_code;
    };

    Result post(const std::string& url, const json& body,
                long timeout_ms = 30000,
                const std::vector<std::string>& extra_headers = {}) const {
        std::string body_str = body.dump();
        std::string response_body;
        std::vector<std::string> resp_headers;
        long http_code = 0;

        auto* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("curl_easy_init failed");
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        for (const auto& h : extra_headers) {
            headers = curl_slist_append(headers, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            throw std::runtime_error(
                "HTTP request failed: " + std::string(curl_easy_strerror(res)));
        }

        if (http_code < 200 || http_code >= 300) {
            throw std::runtime_error(
                "HTTP error " + std::to_string(http_code) + ": " + response_body);
        }

        return {json::parse(response_body), resp_headers, http_code};
    }

    using StreamCallback = std::function<void(const std::string& chunk)>;

    void post_stream(const std::string& url, const json& body,
                     StreamCallback on_chunk,
                     long timeout_ms = 120000,
                     const std::vector<std::string>& extra_headers = {}) const {
        std::string body_str = body.dump();

        auto* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("curl_easy_init failed");
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: text/event-stream");
        for (const auto& h : extra_headers) {
            headers = curl_slist_append(headers, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body_stream);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            throw std::runtime_error(
                "HTTP stream request failed: " + std::string(curl_easy_strerror(res)));
        }
    }

    json get(const std::string& url, long timeout_ms = 10000) const {
        std::string response_body;
        auto* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            throw std::runtime_error("HTTP GET failed: " + std::string(curl_easy_strerror(res)));
        if (http_code < 200 || http_code >= 300)
            throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response_body);

        return json::parse(response_body);
    }

private:
    static size_t write_body(void* contents, size_t size, size_t nmemb, std::string* s) {
        size_t total = size * nmemb;
        s->append(static_cast<char*>(contents), total);
        return total;
    }

    static size_t write_header(void* contents, size_t size, size_t nmemb,
                               std::vector<std::string>* headers) {
        size_t total = size * nmemb;
        std::string header(static_cast<char*>(contents), total);
        if (!header.empty() && header != "\r\n" && header != "\n") {
            headers->push_back(header);
        }
        return total;
    }

    static size_t write_body_stream(void* contents, size_t size, size_t nmemb,
                                    StreamCallback* on_chunk) {
        size_t total = size * nmemb;
        std::string data(static_cast<char*>(contents), total);
        (*on_chunk)(data);
        return total;
    }
};

// ============================================================
// MCP Client (streamable-http transport with session support)
// ============================================================
struct MCPTool {
    std::string name;
    std::string description;
    json input_schema;
};

class MCPClient {
public:
    MCPClient(std::string base_url, const HttpClient& http)
        : base_url_(std::move(base_url)), http_(http) {}

    json send_request(const json& req) {
        std::vector<std::string> extra_headers;
        if (!session_id_.empty()) {
            extra_headers.push_back("Mcp-Session-Id: " + session_id_);
        }

        auto result = http_.post(base_url_, req, 30000, extra_headers);

        if (session_id_.empty()) {
            for (const auto& h : result.response_headers) {
                std::string lower_h = h;
                std::transform(lower_h.begin(), lower_h.end(), lower_h.begin(), ::tolower);
                if (lower_h.find("mcp-session-id:") == 0) {
                    session_id_ = trim(h.substr(std::string("mcp-session-id:").length()));
                    break;
                }
            }
        }

        return result.body;
    }

    json initialize() {
        json req = {
            {"jsonrpc", "2.0"},
            {"id", next_id_++},
            {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", json::object()},
                {"clientInfo", {{"name", "llm-chat"}, {"version", "1.0.0"}}}
            }}
        };
        return send_request(req);
    }

    std::vector<MCPTool> list_tools() {
        json req = {
            {"jsonrpc", "2.0"},
            {"id", next_id_++},
            {"method", "tools/list"},
            {"params", json::object()}
        };
        auto resp = send_request(req);

        if (resp.contains("error")) {
            throw std::runtime_error(
                "MCP error " + resp["error"]["code"].dump() + ": "
                + resp["error"].value("message", "unknown"));
        }

        if (!resp.contains("result") || !resp["result"].contains("tools")) {
            return {};
        }

        std::vector<MCPTool> tools;
        for (const auto& t : resp["result"]["tools"]) {
            MCPTool tool;
            tool.name = t.value("name", "");
            tool.description = t.value("description", "");
            tool.input_schema = t.value("inputSchema", json::object());
            tools.push_back(std::move(tool));
        }
        return tools;
    }

    json call_tool(const std::string& name, const json& arguments) {
        json req = {
            {"jsonrpc", "2.0"},
            {"id", next_id_++},
            {"method", "tools/call"},
            {"params", {
                {"name", name},
                {"arguments", arguments}
            }}
        };
        auto resp = send_request(req);

        if (resp.contains("error")) {
            throw std::runtime_error(
                "MCP tool error " + resp["error"]["code"].dump() + ": "
                + resp["error"].value("message", "unknown"));
        }

        return resp;
    }

    const std::string& session_id() const { return session_id_; }

private:
    std::string base_url_;
    const HttpClient& http_;
    std::string session_id_;
    int next_id_ = 1;
};

// ============================================================
// LLM Client (OpenAI-compatible)
// ============================================================
class LLMClient {
public:
    LLMClient(std::string base_url, const HttpClient& http)
        : chat_url_(base_url + "/v1/chat/completions"), models_url_(base_url + "/v1/models"), http_(http) {}

    struct Usage {
        int prompt_tokens = 0;
        int completion_tokens = 0;
        int total_tokens = 0;
    };

    struct ChatResult {
        json message;
        std::string content;
        std::vector<json> tool_calls;
        std::string finish_reason;
        std::string model_name;
        Usage usage;
        int duration_ms = 0;
    };

    struct ModelInfo {
        std::string name;
        int max_context = 0;
    };

    ModelInfo fetch_model_info() {
        ModelInfo info;
        try {
            auto resp = http_.get(models_url_, 5000);
            auto& data = resp["data"];
            if (!data.empty()) {
                auto& model = data[0];
                info.name = model.value("id", "");
                if (model.contains("meta")) {
                    auto& meta = model["meta"];
                    info.max_context = meta.value("n_ctx", 0);
                    if (info.max_context == 0)
                        info.max_context = meta.value("max_context_length", 0);
                    if (info.max_context == 0)
                        info.max_context = meta.value("n_ctx_train", 0);
                }
            }
        } catch (...) {}
        return info;
    }

    ChatResult chat(const json& messages, const json& tools = json(),
                    const std::string& model = "default") {
        json body = {
            {"model", model},
            {"messages", messages},
            {"stream", false}
        };

        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }

        auto start = std::chrono::steady_clock::now();
        auto result = http_.post(chat_url_, body, 120000, {});
        auto end = std::chrono::steady_clock::now();

        auto& resp = result.body;

        auto& choice = resp["choices"][0];
        auto& msg = choice["message"];

        ChatResult cr;
        cr.message = msg;
        cr.content = msg.value("content", "");
        cr.finish_reason = choice.value("finish_reason", "");
        cr.model_name = resp.value("model", "");
        cr.duration_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        if (resp.contains("usage")) {
            auto& u = resp["usage"];
            cr.usage.prompt_tokens = u.value("prompt_tokens", 0);
            cr.usage.completion_tokens = u.value("completion_tokens", 0);
            cr.usage.total_tokens = u.value("total_tokens", 0);
        }

        if (msg.contains("tool_calls") && !msg["tool_calls"].is_null()) {
            for (const auto& tc : msg["tool_calls"]) {
                cr.tool_calls.push_back(tc);
            }
        }

        return cr;
    }

    using TokenCallback = std::function<void(const std::string& token)>;

    ChatResult chat_stream(const json& messages, TokenCallback on_token,
                           const json& tools = json(),
                           const std::string& model = "default") {
        json body = {
            {"model", model},
            {"messages", messages},
            {"stream", true},
            {"stream_options", {{"include_usage", true}}}
        };

        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }

        std::string buffer;
        std::vector<json> tool_calls_json;
        std::string finish_reason;
        std::string model_name;
        int prompt_tokens = 0;
        int completion_tokens = 0;
        int total_tokens = 0;

        auto start = std::chrono::steady_clock::now();

        http_.post_stream(chat_url_, body,
            [&](const std::string& chunk) {
                buffer += chunk;

                while (true) {
                    size_t pos = buffer.find("\n\n");
                    if (pos == std::string::npos) break;

                    std::string event = buffer.substr(0, pos);
                    buffer.erase(0, pos + 2);

                    for (const auto& line : split_lines(event)) {
                        if (line.substr(0, 6) != "data: ") continue;
                        std::string data = line.substr(6);
                        if (data == "[DONE]") return;

                        try {
                            auto j = json::parse(data);

                            if (j.contains("model"))
                                model_name = j["model"].get<std::string>();

                            if (j.contains("usage")) {
                                auto& u = j["usage"];
                                prompt_tokens = u.value("prompt_tokens", 0);
                                completion_tokens = u.value("completion_tokens", 0);
                                total_tokens = u.value("total_tokens", 0);
                            }

                            if (!j.contains("choices") || j["choices"].empty()) continue;
                            auto& choice = j["choices"][0];

                            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
                                finish_reason = choice["finish_reason"].get<std::string>();

                            if (!choice.contains("delta")) continue;
                            auto& delta = choice["delta"];
                            if (delta.contains("content") && !delta["content"].is_null()) {
                                std::string token = delta["content"].get<std::string>();
                                on_token(token);
                            }

                            if (delta.contains("tool_calls") && !delta["tool_calls"].is_null()) {
                                for (auto& tc : delta["tool_calls"]) {
                                    int idx = tc.value("index", 0);
                                    while (static_cast<int>(tool_calls_json.size()) <= idx) {
                                        tool_calls_json.push_back(json{{"type", "function"}, {"id", ""}, {"function", {{"name", ""}, {"arguments", ""}}}});
                                    }
                                    auto& existing = tool_calls_json[idx];
                                    if (tc.contains("id") && !tc["id"].is_null())
                                        existing["id"] = tc["id"];
                                    if (tc.contains("function")) {
                                        auto& fn = tc["function"];
                                        if (fn.contains("name") && !fn["name"].is_null())
                                            existing["function"]["name"] = fn["name"];
                                        if (fn.contains("arguments") && !fn["arguments"].is_null())
                                            existing["function"]["arguments"] = existing["function"]["arguments"].get<std::string>() + fn["arguments"].get<std::string>();
                                    }
                                }
                            }
                        } catch (...) {}
                    }
                }
            });

        auto end = std::chrono::steady_clock::now();

        json msg = {{"role", "assistant"}, {"content", ""}};
        if (!tool_calls_json.empty()) {
            msg["tool_calls"] = tool_calls_json;
        }

        ChatResult cr;
        cr.message = msg;
        cr.tool_calls = tool_calls_json;
        cr.finish_reason = finish_reason;
        cr.model_name = model_name;
        cr.usage.prompt_tokens = prompt_tokens;
        cr.usage.completion_tokens = completion_tokens;
        cr.usage.total_tokens = total_tokens;
        cr.duration_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        return cr;
    }

private:
    std::string chat_url_;
    std::string models_url_;
    const HttpClient& http_;

    static std::vector<std::string> split_lines(const std::string& s) {
        std::vector<std::string> lines;
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) {
            lines.push_back(line);
        }
        return lines;
    }
};

int get_terminal_width() {
    struct winsize w;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }

    return 80; // fallback
}

// ============================================================
// Chat Application
// ============================================================
class ChatApp {
public:
    ChatApp(std::string llm_url, std::string mcp_url, int max_context = 8192)
        : llm_url_(std::move(llm_url)), mcp_url_(std::move(mcp_url)), max_context_(max_context) {}

    void run() {
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        print_logo();
        std::cout << "\n";

        HttpClient http;
        MCPClient mcp(mcp_url_, http);
        LLMClient llm(llm_url_, http);

        // Fetch model info from server (including max context)
        {
            auto info = llm.fetch_model_info();
            if (info.max_context > 0) {
                max_context_ = info.max_context;
                std::cout << color("Model: ", 90) << info.name
                          << color(", context: ", 90) << max_context_ << "\n";
            }
        }

        // Connect to MCP server
        std::cout << "Connecting to MCP: " << mcp_url_ << " ... ";
        std::cout.flush();
        try {
            auto init_resp = mcp.initialize();
            std::string sid = mcp.session_id();
            std::cout << color("ok", 32);
            if (!sid.empty()) {
                std::cout << " (session: " << sid.substr(0, 16) << "...)";
            }
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cout << color("failed", 31) << " (" << e.what() << ")\n";
        }

        // List MCP tools
        std::vector<MCPTool> tools;
        try {
            tools = mcp.list_tools();
            std::cout << "MCP tools (" << tools.size() << "): ";
            for (size_t i = 0; i < tools.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << color(tools[i].name, 36);
            }
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cout << color("Failed to list tools: ", 33) << e.what() << "\n";
        }

        std::cout << color(std::string(get_terminal_width(), '-'), 90) << "\n";
        print_help();
        std::cout << "\n";

        json messages = json::array();
        json openai_tools = json::array();

        for (const auto& t : tools) {
            json ot = {
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
            char* line = readline(rl_prompt(color(bold(">> "), 32)).c_str());
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
                messages = json::array();
                std::cout << "Chat history cleared.\n";
                continue;
            }
            if (input == "/tools") {
                std::cout << "MCP tools (" << tools.size() << "): ";
                for (size_t i = 0; i < tools.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << color(tools[i].name, 36);
                }
                std::cout << "\n";
                continue;
            }

            json user_msg = {
                {"role", "user"},
                {"content", input}
            };
            messages.push_back(user_msg);

            bool tool_loop = true;
            int tool_call_count = 0;
            const int max_tool_calls = 20;

            while (tool_loop && g_running && tool_call_count < max_tool_calls) {
                try {
                    std::string full_content;
                    bool first_token = true;

                    auto result = llm.chat_stream(messages,
                        [&](const std::string& token) {
                            if (first_token) {
                                std::cout << color(std::string(get_terminal_width(), '-'), 90) << "\n";
                                std::cout << color(bold("Response: "), 36);
                                std::cout.flush();
                                first_token = false;
                            }
                            std::cout << token << std::flush;
                            full_content += token;
                        },
                        openai_tools);

                    if (!first_token) {
                        std::cout << "\n";
                        std::cout << color(std::string(get_terminal_width(), '-'), 90) << "\n";
                    }

                    result.content = full_content;

                    if (result.content.empty() && result.tool_calls.empty()) {
                        std::cout << color("[Model returned empty response]", 33) << "\n";
                        break;
                    }

                    print_stats(result);

                    if (!result.tool_calls.empty()) {
                        json assistant_msg = {
                            {"role", "assistant"},
                            {"content", full_content.empty() ? json() : json(full_content)}
                        };
                        assistant_msg["tool_calls"] = result.tool_calls;
                        messages.push_back(assistant_msg);

                        for (const auto& tc : result.tool_calls) {
                            tool_call_count++;

                            auto& func = tc["function"];
                            std::string tool_name = func["name"];
                            json tool_args = json::parse(func["arguments"].get<std::string>());
                            std::string tool_call_id = tc["id"];

                            std::cout << color("  ⚡ Tool call: ", 33)
                                      << color(bold(tool_name), 33) << "(";
                            bool first = true;
                            for (auto& [key, val] : tool_args.items()) {
                                if (!first) std::cout << ", ";
                                std::cout << key << "=" << val.dump();
                                first = false;
                            }
                            std::cout << ")\n";
                            std::cout.flush();

                            json mcp_result;
                            try {
                                mcp_result = mcp.call_tool(tool_name, tool_args);
                            } catch (const std::exception& e) {
                                mcp_result = {
                                    {"result", {
                                        {"content", json::array({{{"type", "text"}, {"text", std::string("Error: ") + e.what()}}})}
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

                            std::cout << color("  ← Result: ", 32)
                                      << tool_result_str << "\n\n";
                            std::cout.flush();

                            json tool_msg = {
                                {"role", "tool"},
                                {"content", tool_result_str},
                                {"tool_call_id", tool_call_id}
                            };
                            messages.push_back(tool_msg);
                        }
                    } else {
                        json assistant_msg = {
                            {"role", "assistant"},
                            {"content", result.content}
                        };
                        messages.push_back(assistant_msg);
                        tool_loop = false;
                    }

                } catch (const std::exception& e) {
                    std::cout << color(bold("ERROR: "), 31) << e.what() << "\n";
                    break;
                }
            }

            if (tool_call_count >= max_tool_calls) {
                std::cout << color("Maximum tool call count reached.", 33) << "\n";
            }
        }
    }

private:
    void print_logo() {
        std::cout << R"(██      ██      ███    ███        ██████ ██   ██  █████  ████████  )" "\n";
        std::cout << R"(██      ██      ████  ████       ██      ██   ██ ██   ██    ██     )" "\n";
        std::cout << R"(██      ██      ██ ████ ██ █████ ██      ███████ ███████    ██     )" "\n";
        std::cout << R"(██      ██      ██  ██  ██       ██      ██   ██ ██   ██    ██     )" "\n";
        std::cout << R"(███████ ███████ ██      ██        ██████ ██   ██ ██   ██    ██     )" "\n";
    }

    void print_stats(const LLMClient::ChatResult& r) {
        std::ostringstream line;
        line << color(" ─ ", 90);

        int prompt_tokens = r.usage.prompt_tokens;
        int completion_tokens = r.usage.completion_tokens;
        int total_tokens = r.usage.total_tokens;

        if (completion_tokens > 0) {
            int total = total_tokens > 0 ? total_tokens : completion_tokens;
            line << prompt_tokens << " → " << completion_tokens
                 << " tokens - ";
            double ts = r.duration_ms > 0
                ? (completion_tokens * 1000.0 / r.duration_ms) : 0.0;
            line << std::fixed << std::setprecision(1) << ts << " t/s - ";
            if (max_context_ > 0) {
                line << "%" << (total * 100 / max_context_)
                     << " context (" << total << "/" << max_context_ << ") - ";
            }
        }
        line << r.duration_ms << "ms";

        if (!r.model_name.empty()) {
            line << " - ";
            int cols = get_terminal_width();
            std::string raw = line.str();
            size_t visible = 0;
            bool in_escape = false;
            for (char c : raw) {
                if (c == '\033') { in_escape = true; continue; }
                if (in_escape) { if (c == 'm') in_escape = false; continue; }
                visible++;
            }
            int pad = (cols > visible + 2) ? cols - visible - r.model_name.size() - 1 : 2;
            for (int i = 0; i < pad; ++i) line << " ";
            line << r.model_name;
        }

        std::cout << line.str() << "\n";
    }

    void print_help() {
        std::cout << "Commands:\n";
        std::cout << "  " << color("/quit", 33) << " or " << color("/exit", 33) << " — Exit\n";
        std::cout << "  " << color("/help", 33) << " — Show this help\n";
        std::cout << "  " << color("/clear", 33) << " — Clear chat history\n";
        std::cout << "  " << color("/tools", 33) << " — List MCP tools\n";
        std::cout << color(std::string(get_terminal_width(), '-'), 90) << "\n";
    }

    std::string llm_url_;
    std::string mcp_url_;
    int max_context_;
};

} // anonymous namespace

static std::string env_or(const char* key, const std::string& fallback) {
    const char* val = std::getenv(key);
    return val ? std::string(val) : fallback;
}

static int env_int(const char* key, int fallback) {
    const char* val = std::getenv(key);
    if (!val) return fallback;
    try { return std::stoi(val); } catch (...) { return fallback; }
}

int main(int argc, char* argv[]) {
    std::string llm_url = env_or("LLM_CHAT_LLM_URL", "http://localhost:8080");
    std::string mcp_url = env_or("LLM_CHAT_MCP_URL", "http://localhost:8000/mcp");
    int max_context = env_int("LLM_CHAT_MAX_CONTEXT", 8192);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--llm-url" && i + 1 < argc) {
            llm_url = argv[++i];
        } else if (arg == "--mcp-url" && i + 1 < argc) {
            mcp_url = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: llm-chat [--llm-url URL] [--mcp-url URL]\n";
            std::cout << "  LLM_CHAT_LLM_URL (" << llm_url << ")\n";
            std::cout << "  LLM_CHAT_MCP_URL (" << mcp_url << ")\n";
            std::cout << "  LLM_CHAT_MAX_CONTEXT (" << max_context << ")\n";
            return 0;
        }
    }

    try {
        ChatApp app(llm_url, mcp_url, max_context);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << color(bold("CRITICAL ERROR: "), 31) << e.what() << "\n";
        return 1;
    }

    return 0;
}
