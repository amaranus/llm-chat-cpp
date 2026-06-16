#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace http {

class HttpClient {
public:
    using json = nlohmann::json;

    struct Result {
        json body;
        std::vector<std::string> response_headers;
        long http_code;
    };

    using StreamCallback = std::function<void(const std::string& chunk)>;
    using AbortCheck = std::function<bool()>;

    HttpClient();
    ~HttpClient();

    // Non-copyable, movable
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept = default;
    HttpClient& operator=(HttpClient&&) noexcept = default;

    Result post(const std::string& url, const json& body,
                long timeout_ms = 30000,
                const std::vector<std::string>& extra_headers = {},
                AbortCheck abort = nullptr) const;

    void post_stream(const std::string& url, const json& body,
                     StreamCallback on_chunk,
                     long timeout_ms = 120000,
                     const std::vector<std::string>& extra_headers = {},
                     AbortCheck abort = nullptr) const;

    json get(const std::string& url, long timeout_ms = 10000,
             AbortCheck abort = nullptr) const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace http