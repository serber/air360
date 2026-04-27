#include "air360/uploads/adapters/air360_api_uploader.hpp"

#include <string>
#include <utility>

#include "air360/uploads/adapters/air360_json_payload.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/upload_transport.hpp"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.upload";

std::string trimTrailingSlash(const char* url) {
    std::string trimmed = url != nullptr ? url : "";
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}

void replaceAll(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }

    std::size_t position = 0U;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string buildBackendOrigin(const BackendRecord& record) {
    std::string origin = backendProtocolScheme(record.protocol);
    origin += boundedCString(record.host, kBackendHostCapacity);
    if (record.port != 0U && !isDefaultBackendPort(record.protocol, record.port)) {
        origin += ':';
        origin += std::to_string(record.port);
    }
    return origin;
}

std::string buildRegistrationUrl(const BackendRecord& record, const MeasurementBatch& batch) {
    const std::string chip_id = !batch.chip_id.empty() ? batch.chip_id : batch.short_chip_id;
    return buildBackendOrigin(record) + "/v1/devices/" + chip_id + "/register";
}

std::string buildUrl(const BackendRecord& record, const MeasurementBatch& batch) {
    std::string url = buildBackendUrl(record);
    const std::string chip_id = !batch.chip_id.empty() ? batch.chip_id : batch.short_chip_id;

    if (url.find("{chip_id}") != std::string::npos || url.find("{batch_id}") != std::string::npos) {
        replaceAll(url, "{chip_id}", chip_id);
        replaceAll(url, "{batch_id}", std::to_string(batch.batch_id));
        return url;
    }

    const std::string base = trimTrailingSlash(url.c_str());
    return base + "/v1/devices/" + chip_id + "/batches/" + std::to_string(batch.batch_id);
}

}  // namespace

BackendType Air360ApiUploader::type() const {
    return BackendType::kAir360Api;
}

bool Air360ApiUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (record.host[0] == '\0') {
        error = "Air360 host must not be empty.";
        return false;
    }
    if (record.port == 0U) {
        error = "Air360 port must be greater than zero.";
        return false;
    }
    error.clear();
    return true;
}

bool Air360ApiUploader::prepareSync(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    const UploadTransport& transport,
    std::string& error) {
    if (registered_.load(std::memory_order_acquire)) {
        return true;
    }

    if (record.latitude == 0.0F && record.longitude == 0.0F) {
        error = "Air360 location is not set. Configure latitude and longitude in Backends.";
        return false;
    }

    const std::string chip_id = !batch.chip_id.empty() ? batch.chip_id : batch.short_chip_id;

    std::string body = "{\"name\":\"";
    body += jsonEscape(batch.device_name);
    body += "\",\"latitude\":";
    body += std::to_string(record.latitude);
    body += ",\"longitude\":";
    body += std::to_string(record.longitude);
    body += ",\"firmware_version\":\"";
    body += jsonEscape(batch.project_version);
    body += "\"}";

    UploadRequestSpec request;
    request.request_key = std::string("air360_api:register:") + chip_id;
    request.method = UploadMethod::kPut;
    request.url = buildRegistrationUrl(record, batch);
    request.timeout_ms = 15000;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"User-Agent", std::string("air360/") + batch.project_version});
    request.body = std::move(body);

    const UploadTransportResponse response = transport.execute(request);

    if (response.transport_err != ESP_OK) {
        error = std::string("Registration transport error: ") + esp_err_to_name(response.transport_err);
        ESP_LOGW(kTag, "Air360 registration failed (transport): %s", error.c_str());
        return false;
    }

    if (response.http_status >= 200 && response.http_status <= 299) {
        registered_.store(true, std::memory_order_release);
        ESP_LOGI(kTag, "Air360 device registered: %s", chip_id.c_str());
        error.clear();
        return true;
    }

    error = std::string("Registration failed: HTTP ") + std::to_string(response.http_status);
    if (!response.body_snippet.empty()) {
        error += " — ";
        error += response.body_snippet;
    }
    ESP_LOGW(kTag, "Air360 registration failed: %s", error.c_str());
    return false;
}

bool Air360ApiUploader::buildRequests(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    std::vector<UploadRequestSpec>& out_requests,
    std::string& error) const {
    out_requests.clear();
    out_requests.reserve(1U);

    if (!validateConfig(record, error)) {
        return false;
    }

    if (!validateAir360JsonBatch(batch, error)) {
        return false;
    }

    if (batch.points.empty()) {
        error.clear();
        return true;
    }

    UploadRequestSpec request;
    request.request_key = std::string("air360_api:") + std::to_string(batch.batch_id);
    request.method = UploadMethod::kPut;
    request.url = buildUrl(record, batch);
    request.timeout_ms = 15000;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"User-Agent", std::string("air360/") + batch.project_version});
    request.body = buildAir360JsonBody(batch);
    out_requests.push_back(std::move(request));

    error.clear();
    return true;
}

UploadResultClass Air360ApiUploader::classifyResponse(
    const UploadTransportResponse& response) const {
    if (response.transport_err != ESP_OK) {
        return UploadResultClass::kTransportError;
    }

    if ((response.http_status >= 200 && response.http_status <= 208) ||
        response.http_status == 409) {
        return UploadResultClass::kSuccess;
    }

    return UploadResultClass::kHttpError;
}

std::unique_ptr<IBackendUploader> createAir360ApiUploader() {
    return std::make_unique<Air360ApiUploader>();
}

}  // namespace air360
