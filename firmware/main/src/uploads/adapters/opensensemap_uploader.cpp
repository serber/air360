#include "air360/uploads/adapters/opensensemap_uploader.hpp"

#include <string>
#include <utility>
#include <vector>

#include "air360/sensor_format_utils.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/string_utils.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/opensensemap_mapping_repository.hpp"
#include "air360/uploads/upload_transport.hpp"
#include "esp_log.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.upload.osem";
constexpr char kSenseBoxIdPlaceholder[] = "{sensebox_id}";

// Appends or overwrites the value for an openSenseMap sensor ID. A batch can
// carry more than one sample for the same reading (same sensor model and
// phenomenon); the canonical object body holds one entry per sensor, so the
// latest sample in the batch wins.
void upsertLatestValue(
    std::vector<std::pair<std::string, std::string>>& values,
    const char* sensor_id,
    std::string formatted_value) {
    for (auto& entry : values) {
        if (entry.first == sensor_id) {
            entry.second = std::move(formatted_value);
            return;
        }
    }
    values.emplace_back(sensor_id, std::move(formatted_value));
}

// Canonical openSenseMap postNewMeasurements body: a JSON object keyed by
// sensor ID, e.g. {"5df42dc964b874b6e01c36de":"24.1"}.
std::string buildBody(
    const std::vector<std::pair<std::string, std::string>>& values) {
    std::string body = "{";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0U) {
            body += ",";
        }
        body += "\"";
        body += jsonEscape(values[index].first);
        body += "\":\"";
        body += jsonEscape(values[index].second);
        body += "\"";
    }
    body += "}";
    return body;
}

std::string errorMessageFromResponse(const UploadTransportResponse& response) {
    if (response.transport_err != ESP_OK) {
        return esp_err_to_name(response.transport_err);
    }
    if (response.http_status == 401 || response.http_status == 403) {
        return std::string("HTTP ") + std::to_string(response.http_status) +
               " (check the senseBox access token)";
    }
    if (response.http_status == 404) {
        return "HTTP 404 (senseBox ID not found)";
    }
    if (response.http_status == 422) {
        return "HTTP 422 (a mapped sensor ID does not belong to this senseBox)";
    }
    if (response.http_status != 0) {
        return std::string("HTTP ") + std::to_string(response.http_status);
    }
    return "Upload failed.";
}

}  // namespace

BackendType OpenSenseMapUploader::type() const {
    return BackendType::kOpenSenseMap;
}

bool OpenSenseMapUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (record.host[0] == '\0') {
        error = "OpenSenseMap host must not be empty.";
        return false;
    }
    if (record.port == 0U) {
        error = "OpenSenseMap port must be greater than zero.";
        return false;
    }
    const std::string sensebox_id =
        boundedCString(record.opensensemap_sensebox_id, kBackendSenseBoxIdCapacity);
    if (!isValidOpenSenseMapBoxId(sensebox_id)) {
        error = "OpenSenseMap senseBox ID must be a 16-31 character alphanumeric ID.";
        return false;
    }
    const std::string access_token =
        boundedCString(record.opensensemap_access_token, kBackendAccessTokenCapacity);
    if (!isValidOpenSenseMapAccessToken(access_token)) {
        error = "OpenSenseMap access token contains unsupported characters.";
        return false;
    }
    error.clear();
    return true;
}

UploadAttemptResult OpenSenseMapUploader::deliver(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    const BackendDeliveryContext& context) {
    UploadAttemptResult result;
    result.phase = UploadAttemptPhase::kPreflight;

    if (context.opensensemap_mappings == nullptr) {
        result.result = UploadResultClass::kConfigError;
        result.message = "OpenSenseMap sensor mapping is unavailable.";
        return result;
    }

    OpenSenseMapMappingTable mappings;
    const esp_err_t load_err = context.opensensemap_mappings->load(mappings);
    if (load_err != ESP_OK) {
        result.result = UploadResultClass::kConfigError;
        result.message = std::string("Failed to load OpenSenseMap sensor mapping: ") +
                         esp_err_to_name(load_err);
        return result;
    }

    std::string error;
    std::vector<UploadRequestSpec> requests;
    if (!buildRequests(record, batch, mappings, requests, error)) {
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

#if CONFIG_AIR360_LOG_OPENSENSEMAP_HTTP
        ESP_LOGW(kTag, "POST %s", request.url.c_str());
        ESP_LOGW(kTag, "request body: %s", request.body.c_str());
#endif

        context.resetWatchdog("before opensensemap upload request");
        const UploadTransportResponse response = context.http_transport->execute(request);
        context.resetWatchdog("after opensensemap upload request");

#if CONFIG_AIR360_LOG_OPENSENSEMAP_HTTP
        ESP_LOGW(kTag, "response status %d, body: %s",
            response.http_status,
            response.body_snippet.empty() ? "(empty)" : response.body_snippet.c_str());
#endif

        result.status_code = response.http_status;
        result.response_time_ms = response.response_time_ms;
        result.retry_after_seconds = response.retry_after_seconds;
        result.transport_err = response.transport_err;
        result.response_body_snippet = response.body_snippet;
        result.result = classifyResponse(response);
        if (result.result != UploadResultClass::kSuccess) {
            result.message = errorMessageFromResponse(response);
            return result;
        }
    }

    return result;
}

bool OpenSenseMapUploader::buildRequests(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    const OpenSenseMapMappingTable& mappings,
    std::vector<UploadRequestSpec>& out_requests,
    std::string& error) const {
    out_requests.clear();

    if (!validateConfig(record, error)) {
        return false;
    }

    // Each mapped reading resolves to one openSenseMap sensor ID. Readings with
    // no mapping are skipped; duplicate readings collapse to the latest value.
    std::vector<std::pair<std::string, std::string>> values;
    for (const auto& point : batch.points) {
        const char* sensor_id =
            findOpenSenseMapSensorId(mappings, point.sensor_type, point.value_kind);
        if (sensor_id == nullptr) {
            continue;
        }
        upsertLatestValue(values, sensor_id, formatSensorValue(point.value_kind, point.value));
    }

    if (values.empty()) {
        error.clear();
        return true;
    }

    std::string url = buildBackendUrl(record);
    const std::string sensebox_id =
        boundedCString(record.opensensemap_sensebox_id, kBackendSenseBoxIdCapacity);
    const std::size_t placeholder_pos = url.find(kSenseBoxIdPlaceholder);
    if (placeholder_pos != std::string::npos) {
        url.replace(placeholder_pos, sizeof(kSenseBoxIdPlaceholder) - 1U, sensebox_id);
    }

    UploadRequestSpec request;
    request.request_key = "opensensemap";
    request.url = std::move(url);
    request.timeout_ms = 15000;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back(
        {"User-Agent",
         batch.project_version + "/" + batch.device_id + "/" + batch.esp_mac_id});
    const std::string access_token =
        boundedCString(record.opensensemap_access_token, kBackendAccessTokenCapacity);
    if (!access_token.empty()) {
        // openSenseMap reads the box access token from Authorization; the
        // airrohr-style ingest proxy expects x-osem-device-api-key. Sending both
        // keeps either host working.
        request.headers.push_back({"Authorization", access_token});
        request.headers.push_back({"x-osem-device-api-key", access_token});
    }
    request.body = buildBody(values);
    out_requests.push_back(std::move(request));

    error.clear();
    return true;
}

UploadResultClass OpenSenseMapUploader::classifyResponse(
    const UploadTransportResponse& response) const {
    if (response.transport_err != ESP_OK) {
        return UploadResultClass::kTransportError;
    }

    if (response.http_status >= 200 && response.http_status <= 208) {
        return UploadResultClass::kSuccess;
    }

    return UploadResultClass::kHttpError;
}

std::unique_ptr<IBackendUploader> createOpenSenseMapUploader() {
    return std::make_unique<OpenSenseMapUploader>();
}

}  // namespace air360
