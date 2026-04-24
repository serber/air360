#include "air360/web_request_body.hpp"

namespace air360::web {

esp_err_t readRequestBodyWithRetries(
    std::size_t content_len,
    void* receive_context,
    RequestBodyReceiveFn receive,
    int timeout_result,
    std::string& out_body) {
    out_body.clear();
    if (content_len == 0U) {
        return ESP_OK;
    }
    if (content_len > kHttpMaxRequestBodySize) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (receive == nullptr) {
        return ESP_FAIL;
    }

    out_body.resize(content_len);
    std::size_t received_total = 0U;
    int consecutive_timeouts = 0;
    while (received_total < content_len) {
        const int received = receive(
            receive_context,
            out_body.data() + received_total,
            content_len - received_total);
        if (received == timeout_result) {
            ++consecutive_timeouts;
            if (consecutive_timeouts > kMaxRequestBodyReadTimeouts) {
                out_body.clear();
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }
        if (received <= 0) {
            out_body.clear();
            return ESP_FAIL;
        }

        consecutive_timeouts = 0;
        received_total += static_cast<std::size_t>(received);
    }

    return ESP_OK;
}

}  // namespace air360::web
