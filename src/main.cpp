#include <iostream>
#include <string>
#include "chat_app.h"
#include "utils.h"

int main(int argc, char* argv[]) {
    std::string llm_url = utils::env_or("LLM_CHAT_LLM_URL", "http://localhost:8080");
    std::string mcp_url = utils::env_or("LLM_CHAT_MCP_URL", "http://localhost:8000/mcp");
    int max_context = utils::env_int("LLM_CHAT_MAX_CONTEXT", 8192);

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
        std::cerr << utils::color(utils::bold("CRITICAL ERROR: "), 31) << e.what() << "\n";
        return 1;
    }

    return 0;
}