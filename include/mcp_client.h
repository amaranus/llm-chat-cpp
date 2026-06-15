#pragma once

#include <string>
#include <vector>
#include "http_client.h"

namespace mcp {

struct MCPTool {
    std::string name;
    std::string description;
    http::HttpClient::json input_schema;
};

class MCPClient {
public:
    MCPClient(std::string base_url, const http::HttpClient& http);

    http::HttpClient::json initialize();
    std::vector<MCPTool> list_tools();
    http::HttpClient::json call_tool(const std::string& name, const http::HttpClient::json& arguments);
    const std::string& session_id() const;

private:
    std::string base_url_;
    const http::HttpClient& http_;
    std::string session_id_;
    int next_id_ = 1;

    http::HttpClient::json send_request(const http::HttpClient::json& req);
};

} // namespace mcp