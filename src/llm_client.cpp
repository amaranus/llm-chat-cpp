#include "llm_client.h"
#include "utils.h"
#include <chrono>
#include <sstream>

namespace llm {

LLMClient::LLMClient(std::string base_url, const http::HttpClient& http)
    : chat_url_(base_url + "/v1/chat/completions"),
      models_url_(base_url + "/v1/models"),
      router_url_(base_url + "/models"),
      http_(http) {}

void LLMClient::set_abort_check(AbortCheck check) {
    abort_ = std::move(check);
}

LLMClient::ModelInfo LLMClient::fetch_model_info() {
    ModelInfo info;
    try {
        auto resp = http_.get(models_url_, 5000, abort_);
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

std::vector<std::string> LLMClient::fetch_models() {
    std::vector<std::string> models;
    try {
        auto resp = http_.get(models_url_, 5000, abort_);
        auto& data = resp["data"];
        for (auto& m : data) {
            std::string id = m.value("id", "");
            if (!id.empty()) {
                models.push_back(std::move(id));
            }
        }
    } catch (...) {}
    return models;
}

std::vector<LLMClient::ModelStatus> LLMClient::fetch_models_with_status() {
    std::vector<ModelStatus> models;
    try {
        auto resp = http_.get(router_url_, 5000, abort_);
        auto& data = resp["data"];
        for (auto& m : data) {
            ModelStatus ms;
            ms.id = m.value("id", "");
            if (ms.id.empty()) continue;
            if (m.contains("status") && m["status"].contains("value")) {
                ms.status = m["status"]["value"].get<std::string>();
            }
            if (m.contains("status") && m["status"].contains("args")) {
                auto& args = m["status"]["args"];
                for (size_t i = 0; i + 1 < args.size(); ++i) {
                    std::string a = args[i].get<std::string>();
                    if (a == "--ctx-size" || a == "-c") {
                        ms.max_context = std::stoi(args[i + 1].get<std::string>());
                        break;
                    }
                }
            }
            models.push_back(std::move(ms));
        }
    } catch (...) {}
    return models;
}

bool LLMClient::unload_model(const std::string& name) {
    try {
        json body = {{"model", name}};
        auto resp = http_.post(router_url_ + "/unload", body, 30000, {}, abort_);
        return resp.body.value("success", false);
    } catch (...) {
        return false;
    }
}

LLMClient::ChatResult LLMClient::chat(const json& messages, const json& tools,
                                       const std::string& model) {
    json body = {
        {"model", model},
        {"messages", messages},
        {"stream", false},
        {"timings_per_token", true}
    };

    if (!tools.empty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
    }

    auto start = std::chrono::steady_clock::now();
    auto result = http_.post(chat_url_, body, 120000, {}, abort_);
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

    if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null()) {
        cr.reasoning_content = msg["reasoning_content"].get<std::string>();
    }

    if (resp.contains("timings")) {
        cr.tokens_per_second = resp["timings"].value("predicted_per_second", 0.0);
    }

    if (msg.contains("tool_calls") && !msg["tool_calls"].is_null()) {
        for (const auto& tc : msg["tool_calls"]) {
            cr.tool_calls.push_back(tc);
        }
    }

    return cr;
}

LLMClient::ChatResult LLMClient::chat_stream(const json& messages, TokenCallback on_token,
                                              const json& tools, const std::string& model) {
    json body = {
        {"model", model},
        {"messages", messages},
        {"stream", true},
        {"stream_options", {{"include_usage", true}}},
        {"timings_per_token", true}
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
    std::string reasoning_content;
    double tokens_per_second = 0.0;

    auto start = std::chrono::steady_clock::now();

    http_.post_stream(chat_url_, body,
        [&](const std::string& chunk) {
            buffer += chunk;

            while (true) {
                size_t pos = buffer.find("\n\n");
                if (pos == std::string::npos) break;

                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                for (const auto& line : utils::split_lines(event)) {
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

                        if (j.contains("timings")) {
                            tokens_per_second = j["timings"].value("predicted_per_second", 0.0);
                        }

                        if (!j.contains("choices") || j["choices"].empty()) continue;
                        auto& choice = j["choices"][0];

                        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
                            finish_reason = choice["finish_reason"].get<std::string>();

                        if (!choice.contains("delta")) continue;
                        auto& delta = choice["delta"];

                        if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                            std::string token = delta["reasoning_content"].get<std::string>();
                            reasoning_content += token;
                            on_token(token, true);
                        }

                        if (delta.contains("content") && !delta["content"].is_null()) {
                            std::string token = delta["content"].get<std::string>();
                            on_token(token, false);
                        }

                        if (delta.contains("tool_calls") && !delta["tool_calls"].is_null()) {
                            for (auto& tc : delta["tool_calls"]) {
                                int idx = tc.value("index", 0);
                                while (static_cast<int>(tool_calls_json.size()) <= idx) {
                                    tool_calls_json.push_back(json{
                                        {"type", "function"},
                                        {"id", ""},
                                        {"function", {{"name", ""}, {"arguments", ""}}}
                                    });
                                }
                                auto& existing = tool_calls_json[idx];
                                if (tc.contains("id") && !tc["id"].is_null())
                                    existing["id"] = tc["id"];
                                if (tc.contains("function")) {
                                    auto& fn = tc["function"];
                                    if (fn.contains("name") && !fn["name"].is_null())
                                        existing["function"]["name"] = fn["name"];
                                    if (fn.contains("arguments") && !fn["arguments"].is_null())
                                        existing["function"]["arguments"] =
                                            existing["function"]["arguments"].get<std::string>()
                                            + fn["arguments"].get<std::string>();
                                }
                            }
                        }
                    } catch (...) {}
                }
            }
        }, 120000, {}, abort_);

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
    cr.reasoning_content = reasoning_content;
    cr.tokens_per_second = tokens_per_second;
    cr.duration_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    return cr;
}

} // namespace llm