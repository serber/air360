#include "air360/web_server_internal.hpp"

#include <cstddef>

#include "air360/web_request_body.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360::web {

namespace {

constexpr char kTag[] = "air360.web";

int receiveRequestBodyChunk(void* context, char* buffer, std::size_t buffer_size) {
    return httpd_req_recv(static_cast<httpd_req_t*>(context), buffer, buffer_size);
}

}  // namespace

esp_err_t readRequestBody(httpd_req_t* request, std::string& out_body) {
    return readRequestBodyWithRetries(
        request->content_len,
        request,
        receiveRequestBodyChunk,
        HTTPD_SOCK_ERR_TIMEOUT,
        out_body);
}

esp_err_t sendRequestBodyTooLarge(httpd_req_t* request) {
    httpd_resp_set_status(request, "413 Payload Too Large");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Request body exceeds the 4096-byte limit.");
}

esp_err_t sendHtmlResponse(httpd_req_t* request, const std::string& html) {
    // Stream HTML in 1 KB chunks so large pages do not require a second copy in
    // the HTTP server stack or transport buffers.
    constexpr std::size_t kChunkSize = 1024U;

    for (std::size_t offset = 0; offset < html.size(); offset += kChunkSize) {
        const std::size_t remaining = html.size() - offset;
        const std::size_t chunk_size = remaining < kChunkSize ? remaining : kChunkSize;
        const esp_err_t err =
            httpd_resp_send_chunk(request, html.data() + offset, chunk_size);
        if (err != ESP_OK) {
            return err;
        }
    }

    return httpd_resp_send_chunk(request, nullptr, 0);
}

void logHttpHandlerWatermark() {
    const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
    const std::size_t hwm_bytes =
        static_cast<std::size_t>(hwm_words) * sizeof(StackType_t);
    if (hwm_bytes < kHttpdStackBytes / 10U) {
        ESP_LOGW(kTag, "httpd stack critical: %u bytes free of %u (>90%% used)",
                 static_cast<unsigned>(hwm_bytes),
                 static_cast<unsigned>(kHttpdStackBytes));
    } else if (hwm_bytes < (kHttpdStackBytes * 3U) / 10U) {
        ESP_LOGW(kTag, "httpd stack high: %u bytes free of %u (>70%% used)",
                 static_cast<unsigned>(hwm_bytes),
                 static_cast<unsigned>(kHttpdStackBytes));
    } else if (hwm_bytes < kHttpdStackBytes / 2U) {
        ESP_LOGI(kTag, "httpd stack moderate: %u bytes free of %u (>50%% used)",
                 static_cast<unsigned>(hwm_bytes),
                 static_cast<unsigned>(kHttpdStackBytes));
    }
}

}  // namespace air360::web
