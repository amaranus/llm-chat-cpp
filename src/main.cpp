#include <iostream>
#include <string>
#include "chat_app.h"
#include "mcp_config.h"
#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::string llm_url = utils::env_or("LLM_CHAT_LLM_URL", "http://localhost:8080");
    std::string mcp_url = utils::env_or("LLM_CHAT_MCP_URL", "http://localhost:8000/mcp");
    int max_context = utils::env_int("LLM_CHAT_MAX_CONTEXT", 8192);
    std::string mcp_config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--llm-url" && i + 1 < argc) {
            llm_url = argv[++i];
        } else if (arg == "--mcp-url" && i + 1 < argc) {
            mcp_url = argv[++i];
        } else if (arg == "--mcp-config" && i + 1 < argc) {
            mcp_config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: llm-chat [--llm-url URL] [--mcp-url URL] [--mcp-config PATH]\n";
            std::cout << "  LLM_CHAT_LLM_URL (" << llm_url << ")\n";
            std::cout << "  LLM_CHAT_MCP_URL (" << mcp_url << ")\n";
            std::cout << "  LLM_CHAT_MAX_CONTEXT (" << max_context << ")\n";
            return 0;
        }
    }

    std::vector<mcp::MCPServerConfig> mcp_configs;
    if (!mcp_config_path.empty()) {
        mcp_configs = mcp::MCPConfig::load_from_file(mcp_config_path);
    } else {
        std::string found = mcp::MCPConfig::find_config_file();
        if (!found.empty()) {
            mcp_configs = mcp::MCPConfig::load_from_file(found);
            std::cout << "Loaded MCP config from: " << found << "\n";
        }
    }

    if (!mcp_configs.empty() && mcp_url == "http://localhost:8000/mcp") {
        mcp_url = mcp_configs[0].url;
    }

    try {
        ChatApp app(llm_url, mcp_url, mcp_configs, max_context);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << utils::color(utils::bold("CRITICAL ERROR: "), 31) << e.what() << "\n";
        return 1;
    }

    return 0;
}