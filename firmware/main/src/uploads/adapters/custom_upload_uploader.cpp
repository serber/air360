#include "air360/uploads/adapters/custom_upload_uploader.hpp"

#include <memory>
#include <string>
#include <utility>

#include "air360/uploads/adapters/air360_json_payload.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/upload_transport.hpp"

namespace air360 {

namespace {

std::string errorMessageFromResponse(const UploadTransportResponse& response) {
    if (response.transport_err != ESP_OK) {
        return esp_err_to_name(response.transport_err);
    }
    if (!response.body_snippet.empty()) {
        return response.body_snippet;
    }
    if (response.http_status != 0) {
        return std::string("HTTP ") + std::to_string(response.http_status);
    }
    return "Upload failed.";
}

}  // namespace

BackendType CustomUploadUploader::type() const {
    return BackendType::kCustomUpload;
}

bool CustomUploadUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (record.host[0] == '\0') {
        error = "Custom upload host must not be empty.";
        return false;
    }
    if (record.port == 0U) {
        error = "Custom upload port must be greater than zero.";
        return false;
    }
    error.clear();
    return true;
}

UploadAttemptResult CustomUploadUploader::deliver(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    const BackendDeliveryContext& context) {
    UploadAttemptResult result;
    result.phase = UploadAttemptPhase::kPreflight;

    std::string error;
    std::vector<UploadRequestSpec> requests;
    if (!buildRequests(record, batch, requests, error)) {
        result.result = UploadResultClass::kConfigError;
        result.message = std::move(error);
        return result;
    }

    if (requests.empty()) {
        result.result = UploadResultClass::kNoData;
        return result;
    }

    if (context.http_transport == nullptr) {
        result.result = UploadResultClass::kUnsupported;
        result.phase = UploadAttemptPhase::kDataUpload;
        result.message = "HTTP transport is not available.";
        return result;
    }

    result.result = UploadResultClass::kSuccess;
    result.phase = UploadAttemptPhase::kDataUpload;
    for (const auto& request : requests) {
        if (context.stopRequested()) {
            result.result = UploadResultClass::kUnknown;
            result.message = "Upload stopped before request completed.";
            return result;
        }

        context.resetWatchdog("before custom upload request");
        const UploadTransportResponse response = context.http_transport->execute(request);
        context.resetWatchdog("after custom upload request");

        result.status_code = response.http_status;
        result.response_time_ms = response.response_time_ms;
        result.retry_after_seconds = response.retry_after_seconds;
        result.transport_err = response.transport_err;
        result.result = classifyResponse(response);
        if (result.result != UploadResultClass::kSuccess) {
            result.message = errorMessageFromResponse(response);
            return result;
        }
    }

    return result;
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
    request.url = buildBackendUrl(record);
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
