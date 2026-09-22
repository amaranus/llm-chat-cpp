#pragma once

#include <string>
#include <vector>
#include <memory>
#include "http_client.h"
#include "mcp_client.h"
#include "mcp_config.h"
#include "llm_client.h"

struct FileAttachment {
    std::string path;
    std::string filename;
    std::string content;
    std::string mime_type;
    bool is_image;
};

class ChatApp {
public:
    ChatApp(std::string llm_url, std::string mcp_url, int max_context = 8192);
    ChatApp(std::string llm_url, std::string mcp_url, const std::vector<mcp::MCPServerConfig>& mcp_configs, int max_context = 8192);
    void run();

private:
    void print_logo();
    void print_help();
    void print_stats(const llm::LLMClient::ChatResult& r);

    using json = llm::LLMClient::json;

    bool handle_command(const std::string& input,
                        json& messages,
                        std::vector<mcp::MCPTool>& tools,
                        llm::LLMClient& llm);
    void add_file(const std::string& path);
    void remove_file(const std::string& path);
    void list_files();
    void clear_files();

    llm::LLMClient::json build_user_message(const std::string& text);

    bool try_connect_mcp();
    void show_mcp_status();

    std::string llm_url_;
    std::string mcp_url_;
    int max_context_;
    std::string selected_model_ = "default";
    std::vector<FileAttachment> files_;
    std::vector<mcp::MCPServerConfig> mcp_configs_;
    bool mcp_available_ = false;
    http::HttpClient http_;
    std::unique_ptr<mcp::MCPClient> mcp_client_;
};