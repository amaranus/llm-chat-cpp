#pragma once

#include <string>
#include <functional>
#include "http_client.h"

namespace llm {

class LLMClient {
public:
    using json = http::HttpClient::json;

    struct Usage {
        int prompt_tokens = 0;
        int completion_tokens = 0;
        int total_tokens = 0;
    };

    struct ChatResult {
        json message;
        std::string content;
        std::string reasoning_content;
        std::vector<json> tool_calls;
        std::string finish_reason;
        std::string model_name;
        Usage usage;
        double tokens_per_second = 0.0;
        int duration_ms = 0;
    };

    struct ModelInfo {
        std::string name;
        int max_context = 0;
    };

    using TokenCallback = std::function<void(const std::string& token, bool is_reasoning)>;

    LLMClient(std::string base_url, const http::HttpClient& http);

    ModelInfo fetch_model_info();
    ChatResult chat(const json& messages, const json& tools = json(), const std::string& model = "default");
    ChatResult chat_stream(const json& messages, TokenCallback on_token,
                           const json& tools = json(), const std::string& model = "default");

private:
    std::string chat_url_;
    std::string models_url_;
    const http::HttpClient& http_;
};

} // namespace llm