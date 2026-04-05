#include "air360/uploads/upload_transport.hpp"

#include <algorithm>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.http";

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
    config.buffer_size = 512;
    config.buffer_size_tx = 512;
    config.keep_alive_enable = false;
    config.addr_type = HTTP_ADDR_TYPE_INET;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    ESP_LOGI(
        kTag,
        "HTTP request: method=%s url=%s body_len=%u",
        request.method == UploadMethod::kPut ? "PUT" : "POST",
        request.url.c_str(),
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
