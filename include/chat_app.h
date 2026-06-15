#pragma once

#include <string>
#include <vector>
#include "http_client.h"
#include "mcp_client.h"
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
    void run();

private:
    void print_logo();
    void print_help();
    void print_stats(const llm::LLMClient::ChatResult& r);

    bool handle_command(const std::string& input,
                        llm::LLMClient::json& messages);
    void add_file(const std::string& path);
    void remove_file(const std::string& path);
    void list_files();
    void clear_files();

    llm::LLMClient::json build_user_message(const std::string& text);

    std::string llm_url_;
    std::string mcp_url_;
    int max_context_;
    std::vector<FileAttachment> files_;
};