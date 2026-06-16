#include "http_client.h"
#include "utils.h"
#include <stdexcept>
#include <curl/curl.h>

namespace http {

class HttpClient::Impl {
public:
    Impl() { curl_global_init(CURL_GLOBAL_ALL); }
    ~Impl() { curl_global_cleanup(); }

    static size_t write_body(void* contents, size_t size, size_t nmemb, std::string* s) {
        size_t total = size * nmemb;
        s->append(static_cast<char*>(contents), total);
        return total;
    }

    static size_t write_header(void* contents, size_t size, size_t nmemb,
                               std::vector<std::string>* headers) {
        size_t total = size * nmemb;
        std::string header(static_cast<char*>(contents), total);
        if (!header.empty() && header != "\r\n" && header != "\n") {
            headers->push_back(header);
        }
        return total;
    }

    static size_t write_body_stream(void* contents, size_t size, size_t nmemb,
                                    StreamCallback* on_chunk) {
        size_t total = size * nmemb;
        std::string data(static_cast<char*>(contents), total);
        (*on_chunk)(data);
        return total;
    }

    static struct curl_slist* setup_headers(const std::vector<std::string>& extra_headers,
                                            const std::string& accept) {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Accept: " + accept).c_str());
        for (const auto& h : extra_headers) {
            headers = curl_slist_append(headers, h.c_str());
        }
        return headers;
    }

    static int xfer_callback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        auto* abort = static_cast<AbortCheck*>(clientp);
        return (abort && *abort && (*abort)()) ? 1 : 0;
    }

    static void set_common_opts(CURL* curl, const std::string& url,
                                const std::string& body_str,
                                struct curl_slist* headers,
                                long timeout_ms) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);
    }

    static void set_abort_opts(CURL* curl, AbortCheck& abort) {
        if (abort) {
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_callback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &abort);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        }
    }

    static void check_error(CURLcode res, long http_code, const std::string& response_body,
                            const std::string& context) {
        if (res != CURLE_OK) {
            throw std::runtime_error(context + " failed: " + std::string(curl_easy_strerror(res)));
        }
        if (http_code < 200 || http_code >= 300) {
            throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response_body);
        }
    }
};

HttpClient::HttpClient() : pimpl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

HttpClient::Result HttpClient::post(const std::string& url, const json& body,
                                    long timeout_ms,
                                    const std::vector<std::string>& extra_headers,
                                    AbortCheck abort) const {
    std::string body_str = body.dump();
    std::string response_body;
    std::vector<std::string> resp_headers;

    auto* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    auto* headers = Impl::setup_headers(extra_headers, "application/json");
    Impl::set_common_opts(curl, url, body_str, headers, timeout_ms);
    Impl::set_abort_opts(curl, abort);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Impl::write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);

    long http_code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    Impl::check_error(res, http_code, response_body, "HTTP POST");
    return {json::parse(response_body), resp_headers, http_code};
}

void HttpClient::post_stream(const std::string& url, const json& body,
                             StreamCallback on_chunk,
                             long timeout_ms,
                             const std::vector<std::string>& extra_headers,
                             AbortCheck abort) const {
    std::string body_str = body.dump();

    auto* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    auto* headers = Impl::setup_headers(extra_headers, "text/event-stream");
    Impl::set_common_opts(curl, url, body_str, headers, timeout_ms);
    Impl::set_abort_opts(curl, abort);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::write_body_stream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_chunk);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("HTTP stream request failed: " + std::string(curl_easy_strerror(res)));
    }
}

HttpClient::json HttpClient::get(const std::string& url, long timeout_ms,
                                  AbortCheck abort) const {
    std::string response_body;

    auto* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::write_body);
    Impl::set_abort_opts(curl, abort);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);

    long http_code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    Impl::check_error(res, http_code, response_body, "HTTP GET");
    return json::parse(response_body);
}

} // namespace http