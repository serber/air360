#include "air360/uploads/upload_transport.hpp"

#include <algorithm>

#include "esp_http_client.h"

namespace air360 {

namespace {

esp_http_client_method_t toEspMethod(UploadMethod method) {
    switch (method) {
        case UploadMethod::kPost:
        default:
            return HTTP_METHOD_POST;
    }
}

}  // namespace

UploadTransportResponse UploadTransport::execute(const UploadRequestSpec& request) const {
    UploadTransportResponse response{};

    esp_http_client_config_t config{};
    config.url = request.url.c_str();
    config.method = toEspMethod(request.method);
    config.timeout_ms = request.timeout_ms;
    config.disable_auto_redirect = true;
    config.buffer_size = 512;
    config.buffer_size_tx = 512;
    config.keep_alive_enable = false;

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

    esp_http_client_cleanup(client);
    return response;
}

}  // namespace air360
