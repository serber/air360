#include "air360/uploads/adapters/custom_upload_uploader.hpp"

#include <memory>
#include <string>
#include <utility>

#include "air360/uploads/adapters/air360_json_payload.hpp"

namespace air360 {

BackendType CustomUploadUploader::type() const {
    return BackendType::kCustomUpload;
}

bool CustomUploadUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (record.endpoint_url[0] == '\0') {
        error = "Custom upload endpoint URL is empty.";
        return false;
    }

    error.clear();
    return true;
}

bool CustomUploadUploader::buildRequests(
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
    request.request_key = std::string("custom_upload:") + std::to_string(batch.batch_id);
    request.method = UploadMethod::kPost;
    request.url = record.endpoint_url;
    request.timeout_ms = 15000;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"User-Agent", std::string("air360/") + batch.project_version});
    request.body = buildAir360JsonBody(batch);
    out_requests.push_back(std::move(request));

    error.clear();
    return true;
}

UploadResultClass CustomUploadUploader::classifyResponse(
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

std::unique_ptr<IBackendUploader> createCustomUploadUploader() {
    return std::make_unique<CustomUploadUploader>();
}

}  // namespace air360
