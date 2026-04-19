#include "air360/uploads/adapters/air360_api_uploader.hpp"

#include <string>
#include <utility>

#include "air360/uploads/adapters/air360_json_payload.hpp"
#include "air360/uploads/backend_http_config.hpp"

namespace air360 {

namespace {

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

std::string buildUrl(const BackendRecord& record, const MeasurementBatch& batch) {
    BackendHttpConfigView config;
    std::string url;
    std::string error;
    if (!decodeBackendHttpRecord(record, config, error) ||
        !buildBackendHttpUrl(config, url, error)) {
        return "";
    }
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

const char* Air360ApiUploader::backendKey() const {
    return "air360_api";
}

bool Air360ApiUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    return validateBackendHttpRecord(record, error);
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
