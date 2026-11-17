#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "esp_err.h"

enum esp_http_client_method_t { HTTP_METHOD_POST, HTTP_METHOD_PUT };
enum { HTTP_EVENT_ON_HEADER, HTTP_EVENT_ON_DATA };
constexpr int HTTP_ADDR_TYPE_INET = 0;
constexpr esp_err_t ESP_ERR_HTTP_FETCH_HEADER = 0x7002;
struct esp_http_client_event_t {
    int event_id = 0;
    void* user_data = nullptr;
    char* header_key = nullptr;
    char* header_value = nullptr;
    void* data = nullptr;
    int data_len = 0;
};
struct esp_http_client_config_t {
    const char* url = nullptr;
    esp_http_client_method_t method = HTTP_METHOD_POST;
    int timeout_ms = 0;
    bool disable_auto_redirect = false;
    int buffer_size = 0;
    int buffer_size_tx = 0;
    bool keep_alive_enable = false;
    int addr_type = 0;
    esp_err_t (*crt_bundle_attach)(void*) = nullptr;
    esp_err_t (*event_handler)(esp_http_client_event_t*) = nullptr;
    void* user_data = nullptr;
};
struct HttpTestClient {
    esp_http_client_config_t config;
    std::map<std::string, std::string> request_headers;
    std::string url;
    std::string body;
};
using esp_http_client_handle_t = HttpTestClient*;
namespace http_test {
// Response script. response_headers/body are set before requests start;
// the scalar knobs are atomic so threaded tests may change them while an
// upload task is running.
inline std::vector<std::pair<std::string, std::string>> response_headers;
inline std::string body;
inline std::atomic<int> status{429};
inline std::atomic<esp_err_t> result{ESP_OK};
inline std::atomic<unsigned> cleaned_up{0};
inline std::atomic<int> perform_delay_ms{0};
inline std::atomic<int> perform_calls{0};

struct Request {
    std::string url;
    std::string body;
};

// Guarded by mutex: per-URL status overrides (first substring match wins)
// and a log of every performed request.
inline std::mutex mutex;
inline std::vector<std::pair<std::string, int>> status_by_url_substring;
inline std::vector<Request> requests;

inline void reset() {
    std::lock_guard<std::mutex> guard(mutex);
    status_by_url_substring.clear();
    requests.clear();
    response_headers.clear();
    body.clear();
    status = 200;
    result = ESP_OK;
    perform_delay_ms = 0;
    perform_calls = 0;
}

inline int statusFor(const std::string& url) {
    std::lock_guard<std::mutex> guard(mutex);
    for (const auto& [needle, code] : status_by_url_substring) {
        if (url.find(needle) != std::string::npos) {
            return code;
        }
    }
    return status.load();
}
}
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    return new HttpTestClient{*config, {}, config->url != nullptr ? config->url : "", {}};
}
inline esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char* key,
                                          const char* value) {
    client->request_headers[key] = value;
    return ESP_OK;
}
inline esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char* key,
                                          char** value) {
    auto it = client->request_headers.find(key);
    *value = it == client->request_headers.end() ? nullptr : it->second.data();
    return ESP_OK;
}
inline esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char* data,
                                                 int len) {
    client->body.assign(data != nullptr ? data : "", data != nullptr && len > 0 ? len : 0);
    return ESP_OK;
}
inline esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    ++http_test::perform_calls;
    const int delay_ms = http_test::perform_delay_ms.load();
    if (delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    {
        std::lock_guard<std::mutex> guard(http_test::mutex);
        http_test::requests.push_back(http_test::Request{client->url, client->body});
    }
    esp_http_client_event_t event;
    event.user_data = client->config.user_data;
    event.event_id = HTTP_EVENT_ON_HEADER;
    for (auto& header : http_test::response_headers) {
        event.header_key = header.first.data();
        event.header_value = header.second.data();
        client->config.event_handler(&event);
    }
    event.event_id = HTTP_EVENT_ON_DATA;
    event.data = http_test::body.data();
    event.data_len = static_cast<int>(http_test::body.size());
    client->config.event_handler(&event);
    return http_test::result;
}
inline int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return http_test::statusFor(client->url);
}
inline std::int64_t esp_http_client_get_content_length(esp_http_client_handle_t) {
    return http_test::body.size();
}
inline esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    ++http_test::cleaned_up;
    delete client;
    return ESP_OK;
}
