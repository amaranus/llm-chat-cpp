#pragma once

#include <string>
#include <vector>
#include "http_client.h"

namespace mcp {

struct MCPServerConfig {
    std::string name;
    std::string url;
    std::string type = "http";
    http::HttpClient::json headers = http::HttpClient::json::object();
    bool enabled = true;
};

class MCPConfig {
public:
    static std::vector<MCPServerConfig> load_from_file(const std::string& path);
    static std::vector<MCPServerConfig> load_from_exe_dir();
    static std::vector<MCPServerConfig> load_from_cwd();
    static std::string find_config_file();
};

} // namespace mcp
