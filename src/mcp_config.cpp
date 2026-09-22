#include "mcp_config.h"
#include "utils.h"
#include <fstream>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace mcp {

std::vector<MCPServerConfig> MCPConfig::load_from_file(const std::string& path) {
    std::vector<MCPServerConfig> configs;

    std::ifstream file(path);
    if (!file.is_open()) {
        return configs;
    }

    try {
        http::HttpClient::json j;
        file >> j;

        if (!j.contains("mcpServers") || !j["mcpServers"].is_object()) {
            return configs;
        }

        for (auto& [name, server] : j["mcpServers"].items()) {
            MCPServerConfig cfg;
            cfg.name = name;

            if (server.contains("url")) {
                cfg.url = server["url"].get<std::string>();
            }

            if (server.contains("type")) {
                cfg.type = server["type"].get<std::string>();
            }

            if (server.contains("headers")) {
                cfg.headers = server["headers"];
            }

            if (server.contains("enabled")) {
                cfg.enabled = server["enabled"].get<bool>();
            }

            if (!cfg.url.empty() && cfg.enabled) {
                configs.push_back(std::move(cfg));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << utils::color("MCP config parse error: ", 33) << e.what() << "\n";
    }

    return configs;
}

std::vector<MCPServerConfig> MCPConfig::load_from_exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    fs::path exe_path(buf);
    fs::path config_path = exe_path.parent_path() / "mcp.json";
#else
    fs::path config_path = fs::current_path() / "mcp.json";
#endif
    return load_from_file(config_path.string());
}

std::vector<MCPServerConfig> MCPConfig::load_from_cwd() {
    fs::path config_path = fs::current_path() / "mcp.json";
    return load_from_file(config_path.string());
}

std::string MCPConfig::find_config_file() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    fs::path exe_path(buf);
    fs::path exe_dir_config = exe_path.parent_path() / "mcp.json";
    if (fs::exists(exe_dir_config)) {
        return exe_dir_config.string();
    }
#endif

    fs::path cwd_config = fs::current_path() / "mcp.json";
    if (fs::exists(cwd_config)) {
        return cwd_config.string();
    }

    return "";
}

} // namespace mcp
