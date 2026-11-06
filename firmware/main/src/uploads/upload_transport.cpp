#include "air360/uploads/upload_transport.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <strings.h>
#include <utility>

#include "air360/uploads/upload_log_endpoint.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.http";
constexpr std::size_t kMaxBodySnippetBytes = 512U;

struct ResponseBodyCapture {
    std::string snippet;
    bool truncated = false;
    std::uint32_t retry_after_seconds = 0U;
};

std::uint32_t parseRetryAfter(const char* header) {
    if (header == nullptr) {
        return 0U;
    }
    std::string_view value(header);
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return 0U;
    }
    value = value.substr(first, value.find_last_not_of(" \t") - first + 1U);
    std::uint32_t seconds = 0U;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                   seconds <= 3600U
               ? seconds
               : 0U;
}

void appendBodySnippet(ResponseBodyCapture& capture, const char* data, std::size_t data_len) {
    if (data == nullptr || data_len == 0U || capture.snippet.size() >= kMaxBodySnippetBytes) {
        if (data_len > 0U) {
            capture.truncated = true;
        }
        return;
    }

    const std::size_t available = kMaxBodySnippetBytes - capture.snippet.size();
    const std::size_t copy_len = std::min(data_len, available);
    capture.snippet.reserve(kMaxBodySnippetBytes);
    for (std::size_t index = 0U; index < copy_len; ++index) {
        const unsigned char ch = static_cast<unsigned char>(data[index]);
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            capture.snippet.push_back(' ');
        } else if (std::isprint(ch) != 0) {
            capture.snippet.push_back(static_cast<char>(ch));
        } else {
            capture.snippet.push_back('?');
        }
    }

    if (copy_len < data_len) {
        capture.truncated = true;
    }
}

esp_err_t httpEventHandler(esp_http_client_event_t* event) {
    if (event == nullptr) {
        return ESP_OK;
    }

    auto* capture = static_cast<ResponseBodyCapture*>(event->user_data);
    if (capture == nullptr) {
        return ESP_OK;
    }

    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key != nullptr &&
        strcasecmp(event->header_key, "Retry-After") == 0) {
        capture->retry_after_seconds = parseRetryAfter(event->header_value);
    }
    if (event->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }

    const std::size_t data_len =
        event->data_len > 0 ? static_cast<std::size_t>(event->data_len) : 0U;
    appendBodySnippet(*capture, static_cast<const char*>(event->data), data_len);
    return ESP_OK;
}

esp_http_client_method_t toEspMethod(UploadMethod method) {
    switch (method) {
        case UploadMethod::kPut:
            return HTTP_METHOD_PUT;
        case UploadMethod::kPost:
        default:
            return HTTP_METHOD_POST;
    }
}

}  // namespace

UploadTransportResponse UploadTransport::execute(const UploadRequestSpec& request) const {
    UploadTransportResponse response{};
    const std::int64_t started_us = esp_timer_get_time();

    esp_http_client_config_t config{};
    config.url = request.url.c_str();
    config.method = toEspMethod(request.method);
    config.timeout_ms = request.timeout_ms;
    config.disable_auto_redirect = true;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;
    config.keep_alive_enable = false;
    config.addr_type = HTTP_ADDR_TYPE_INET;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    ResponseBodyCapture body_capture;
    config.event_handler = httpEventHandler;
    config.user_data = &body_capture;

    const std::string endpoint = formatUploadEndpointForLog(request.url);
    ESP_LOGI(
        kTag,
        "HTTP request: method=%s endpoint=%s body_len=%u",
        request.method == UploadMethod::kPut ? "PUT" : "POST",
        endpoint.c_str(),
        static_cast<unsigned>(request.body.size()));

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        response.transport_err = ESP_FAIL;
        response.body_snippet = "esp_http_client_init failed";
        return response;
    }

    for (const auto& header : request.headers) {
        const esp_err_t header_err =
            esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
        if (header_err != ESP_OK) {
            response.transport_err = header_err;
            response.body_snippet = "failed to set request header";
            esp_http_client_cleanup(client);
            return response;
        }
    }

    if (!request.body.empty()) {
        const esp_err_t body_err =
            esp_http_client_set_post_field(client, request.body.c_str(), request.body.size());
        if (body_err != ESP_OK) {
            response.transport_err = body_err;
            response.body_snippet = "failed to set request body";
            esp_http_client_cleanup(client);
            return response;
        }
    }

    response.transport_err = esp_http_client_perform(client);
    if (response.transport_err == ESP_OK) {
        response.http_status = esp_http_client_get_status_code(client);
        const auto content_length = esp_http_client_get_content_length(client);
        response.response_size = content_length > 0 ? static_cast<int>(content_length) : 0;

        response.retry_after_seconds = body_capture.retry_after_seconds;
        response.body_snippet = std::move(body_capture.snippet);
        if (body_capture.truncated) {
            response.body_snippet += "...";
        }
    } else if (response.transport_err == ESP_ERR_HTTP_FETCH_HEADER) {
        ESP_LOGE(kTag, "HTTP header parse failed (buffer too small?): %s", endpoint.c_str());
    }

    const std::int64_t finished_us = esp_timer_get_time();
    if (finished_us > started_us) {
        response.response_time_ms =
            static_cast<std::uint32_t>((finished_us - started_us) / 1000LL);
    }

    esp_http_client_cleanup(client);
    return response;
}

}  // namespace air360
