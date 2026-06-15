#pragma once

#include <string>
#include "http_client.h"
#include "mcp_client.h"
#include "llm_client.h"

class ChatApp {
public:
    ChatApp(std::string llm_url, std::string mcp_url, int max_context = 8192);
    void run();

private:
    void print_logo();
    void print_help();
    void print_stats(const llm::LLMClient::ChatResult& r);

    std::string llm_url_;
    std::string mcp_url_;
    int max_context_;
};