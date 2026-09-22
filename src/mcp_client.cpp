#include "mcp_client.h"
#include "utils.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace mcp {

MCPClient::MCPClient(std::string base_url, const http::HttpClient& http)
    : base_url_(std::move(base_url)), http_(http) {}

http::HttpClient::json MCPClient::send_request(const http::HttpClient::json& req) {
    std::vector<std::string> extra_headers;
    extra_headers.push_back("Accept: application/json, text/event-stream");
    if (!session_id_.empty()) {
        extra_headers.push_back("Mcp-Session-Id: " + session_id_);
    }

    auto result = http_.post_raw(base_url_, req, 30000, extra_headers);

    if (session_id_.empty()) {
        for (const auto& h : result.response_headers) {
            std::string lower_h = h;
            std::transform(lower_h.begin(), lower_h.end(), lower_h.begin(), ::tolower);
            if (lower_h.find("mcp-session-id:") == 0) {
                session_id_ = utils::trim(h.substr(std::string("mcp-session-id:").length()));
                break;
            }
        }
    }

    std::string raw = result.body;

    if (!raw.empty() && raw[0] != '{' && raw[0] != '[') {
        std::string json_payload;
        bool in_data = false;
        std::istringstream stream(raw);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("data: ") == 0) {
                json_payload = line.substr(6);
                in_data = true;
            } else if (in_data && !line.empty() && line[0] != ':') {
                break;
            }
        }
        if (!json_payload.empty()) {
            return http::HttpClient::json::parse(json_payload);
        }
    }

    return http::HttpClient::json::parse(raw);
}

http::HttpClient::json MCPClient::initialize() {
    http::HttpClient::json req = {
        {"jsonrpc", "2.0"},
        {"id", next_id_++},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", http::HttpClient::json::object()},
            {"clientInfo", {{"name", "llm-chat"}, {"version", "1.0.0"}}}
        }}
    };
    return send_request(req);
}

std::vector<MCPTool> MCPClient::list_tools() {
    http::HttpClient::json req = {
        {"jsonrpc", "2.0"},
        {"id", next_id_++},
        {"method", "tools/list"},
        {"params", http::HttpClient::json::object()}
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
        tool.input_schema = t.value("inputSchema", http::HttpClient::json::object());
        tools.push_back(std::move(tool));
    }
    return tools;
}

http::HttpClient::json MCPClient::call_tool(const std::string& name, const http::HttpClient::json& arguments) {
    http::HttpClient::json req = {
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

const std::string& MCPClient::session_id() const {
    return session_id_;
}

bool MCPClient::is_connected() const {
    return !session_id_.empty();
}

void MCPClient::disconnect() {
    session_id_.clear();
    next_id_ = 1;
}

void MCPClient::set_url(const std::string& url) {
    base_url_ = url;
}

const std::string& MCPClient::url() const {
    return base_url_;
}

} // namespace mcp