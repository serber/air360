#include "air360/web_server.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "air360/ota_service.hpp"
#include "air360/string_utils.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.web";
// Streaming chunk for the OTA upload path. Sized to keep the httpd task stack
// (16 KB total) comfortably under the FreeRTOS canary threshold even while
// pending data is queued on lwIP — esp_ota_write itself works fine on much
// larger chunks but the bottleneck here is socket recv buffering.
constexpr std::size_t kOtaChunkBytes = 4096U;
// httpd_req_recv timeout retries before we treat the connection as broken.
constexpr int kMaxOtaRecvTimeouts = 5;
// Maximum content-length we accept.  ota_0 / ota_1 are 6 MB each, but we cap
// at 4 MB to leave a margin: full firmware images today are around 1.5 MB.
constexpr std::uint32_t kMaxOtaPayloadBytes = 4U * 1024U * 1024U;

const char* otaStateLabel(OtaState state) {
    switch (state) {
        case OtaState::kIdle:        return "idle";
        case OtaState::kInProgress:  return "in_progress";
        case OtaState::kSuccess:     return "success";
        case OtaState::kError:       return "error";
    }
    return "idle";
}

std::string buildOtaStatusJson(const OtaStatus& status) {
    std::string json;
    json.reserve(384U);
    json += "{\"state\":\"";
    json += otaStateLabel(status.state);
    json += "\",\"bytes_written\":";
    json += std::to_string(status.bytes_written);
    json += ",\"content_length\":";
    json += std::to_string(status.content_length);
    json += ",\"running_version\":\"";
    json += jsonEscape(status.running_version);
    json += "\",\"running_slot\":\"";
    json += jsonEscape(status.running_slot_label);
    json += "\",\"target_slot\":\"";
    json += jsonEscape(status.target_slot_label);
    json += "\",\"target_slot_size\":";
    json += std::to_string(status.target_slot_size_bytes);
    json += ",\"pending_version\":\"";
    json += jsonEscape(status.pending_version);
    json += "\",\"rollback_armed\":";
    json += status.rollback_armed ? "true" : "false";
    json += ",\"error\":\"";
    json += jsonEscape(status.error);
    json += "\"}";
    return json;
}

esp_err_t sendOtaJsonStatus(httpd_req_t* request, const OtaStatus& status, int http_status_code) {
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (http_status_code == 400) {
        httpd_resp_set_status(request, "400 Bad Request");
    } else if (http_status_code == 409) {
        httpd_resp_set_status(request, "409 Conflict");
    } else if (http_status_code == 413) {
        httpd_resp_set_status(request, "413 Payload Too Large");
    } else if (http_status_code == 500) {
        httpd_resp_set_status(request, "500 Internal Server Error");
    } else if (http_status_code == 503) {
        httpd_resp_set_status(request, "503 Service Unavailable");
    }
    const std::string json = buildOtaStatusJson(status);
    return httpd_resp_sendstr(request, json.c_str());
}

}  // namespace

esp_err_t WebServer::handleOtaStatus(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const OtaStatus status = server->ota_service_->snapshot();
    return sendOtaJsonStatus(request, status, 200);
}

esp_err_t WebServer::handleOtaUpload(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    OtaService& ota = *server->ota_service_;

    const int content_len = request->content_len;
    if (content_len <= 0) {
        ESP_LOGW(kTag, "OTA upload rejected: missing Content-Length");
        return sendOtaJsonStatus(request, ota.snapshot(), 400);
    }
    if (static_cast<std::uint32_t>(content_len) > kMaxOtaPayloadBytes) {
        ESP_LOGW(
            kTag,
            "OTA upload rejected: %d bytes exceeds %u-byte limit",
            content_len,
            static_cast<unsigned>(kMaxOtaPayloadBytes));
        return sendOtaJsonStatus(request, ota.snapshot(), 413);
    }

    const esp_err_t begin_err =
        ota.begin(static_cast<std::uint32_t>(content_len));
    if (begin_err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "OTA upload rejected: transfer already in progress");
        return sendOtaJsonStatus(request, ota.snapshot(), 409);
    }
    if (begin_err != ESP_OK) {
        ESP_LOGE(
            kTag, "OTA begin failed: %s", esp_err_to_name(begin_err));
        return sendOtaJsonStatus(request, ota.snapshot(), 503);
    }

    std::array<char, kOtaChunkBytes> buffer{};
    int remaining = content_len;
    int consecutive_timeouts = 0;
    while (remaining > 0) {
        const int request_size =
            (remaining > static_cast<int>(buffer.size())) ? static_cast<int>(buffer.size())
                                                          : remaining;
        const int received =
            httpd_req_recv(request, buffer.data(), static_cast<std::size_t>(request_size));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            ++consecutive_timeouts;
            if (consecutive_timeouts > kMaxOtaRecvTimeouts) {
                ota.abortTransfer("client_timeout");
                ESP_LOGW(kTag, "OTA upload aborted: repeated socket timeouts");
                return sendOtaJsonStatus(request, ota.snapshot(), 500);
            }
            continue;
        }
        if (received <= 0) {
            ota.abortTransfer("client_disconnected");
            ESP_LOGW(kTag, "OTA upload aborted: recv returned %d", received);
            return sendOtaJsonStatus(request, ota.snapshot(), 500);
        }
        consecutive_timeouts = 0;

        const esp_err_t write_err = ota.writeChunk(
            buffer.data(), static_cast<std::size_t>(received));
        if (write_err != ESP_OK) {
            // writeChunk already called esp_ota_abort and populated status.
            return sendOtaJsonStatus(request, ota.snapshot(), 500);
        }
        remaining -= received;
    }

    const esp_err_t commit_err = ota.commitAndScheduleReboot();
    if (commit_err != ESP_OK) {
        return sendOtaJsonStatus(request, ota.snapshot(), 500);
    }

    ESP_LOGI(kTag, "OTA upload completed successfully; reboot scheduled");
    return sendOtaJsonStatus(request, ota.snapshot(), 200);
}

esp_err_t WebServer::handleOtaRollback(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);

    char query[16] = {};
    const esp_err_t query_err =
        httpd_req_get_url_query_str(request, query, sizeof(query));
    if (query_err != ESP_OK ||
        std::strstr(query, "confirm=yes") == nullptr) {
        httpd_resp_set_status(request, "400 Bad Request");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(
            request,
            "{\"error\":\"confirm=yes required\"}");
    }

    const esp_err_t err = server->ota_service_->requestRollback();
    // requestRollback only returns on failure (success reboots the device).
    httpd_resp_set_status(request, "500 Internal Server Error");
    httpd_resp_set_type(request, "application/json");
    std::string response;
    response.reserve(64U);
    response += "{\"error\":\"";
    response += jsonEscape(std::string(esp_err_to_name(err)));
    response += "\"}";
    return httpd_resp_sendstr(request, response.c_str());
}

}  // namespace air360
