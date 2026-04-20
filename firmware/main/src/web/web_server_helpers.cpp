#include "air360/web_server_internal.hpp"

#include <cstddef>

namespace air360::web {

namespace {

constexpr std::size_t kHttpMaxRequestBodySize = 4096U;

}  // namespace

esp_err_t readRequestBody(httpd_req_t* request, std::string& out_body) {
    out_body.clear();
    if (request->content_len <= 0) {
        return ESP_OK;
    }
    if (request->content_len > static_cast<int>(kHttpMaxRequestBodySize)) {
        return ESP_ERR_INVALID_SIZE;
    }

    out_body.resize(static_cast<std::size_t>(request->content_len));
    int received_total = 0;
    while (received_total < request->content_len) {
        const int received = httpd_req_recv(
            request,
            out_body.data() + received_total,
            request->content_len - received_total);
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += received;
    }

    return ESP_OK;
}

esp_err_t sendRequestBodyTooLarge(httpd_req_t* request) {
    httpd_resp_set_status(request, "413 Payload Too Large");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Request body exceeds the 4096-byte limit.");
}

esp_err_t sendHtmlResponse(httpd_req_t* request, const std::string& html) {
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

}  // namespace air360::web
