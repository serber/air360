#include "air360/uploads/adapters/influxdb_uploader.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensor_format_utils.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/string_utils.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/upload_transport.hpp"
#include "mbedtls/base64.h"

namespace air360 {

namespace {

struct InfluxSampleGroup {
    std::uint32_t sensor_id = 0U;
    SensorType sensor_type = SensorType::kUnknown;
    std::uint64_t sample_time_ms = 0U;
    std::vector<std::pair<SensorValueKind, float>> values;
};

std::string escapeMeasurementPart(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == ',' || ch == ' ' || ch == '=') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

InfluxSampleGroup* findGroup(
    std::vector<InfluxSampleGroup>& groups,
    std::uint32_t sensor_id,
    SensorType sensor_type,
    std::uint64_t sample_time_ms) {
    for (auto& group : groups) {
        if (group.sensor_id == sensor_id &&
            group.sensor_type == sensor_type &&
            group.sample_time_ms == sample_time_ms) {
            return &group;
        }
    }
    return nullptr;
}

void upsertLatestValue(InfluxSampleGroup& group, SensorValueKind kind, float value) {
    for (auto& entry : group.values) {
        if (entry.first == kind) {
            entry.second = value;
            return;
        }
    }
    group.values.emplace_back(kind, value);
}

std::string buildLineProtocolBody(
    const MeasurementBatch& batch,
    const std::string& measurement) {
    std::vector<InfluxSampleGroup> groups;
    groups.reserve(batch.points.size());
    for (const auto& point : batch.points) {
        InfluxSampleGroup* group =
            findGroup(groups, point.sensor_id, point.sensor_type, point.sample_time_ms);
        if (group == nullptr) {
            groups.push_back(
                InfluxSampleGroup{point.sensor_id, point.sensor_type, point.sample_time_ms, {}});
            group = &groups.back();
        }
        upsertLatestValue(*group, point.value_kind, point.value);
    }

    const std::string node =
        !batch.short_chip_id.empty() ? batch.short_chip_id : batch.chip_id;
    const std::string escaped_measurement = escapeMeasurementPart(measurement);
    const std::string escaped_node = escapeMeasurementPart(node);

    std::string body;
    body.reserve(256U + groups.size() * 96U);
    for (const auto& group : groups) {
        if (group.values.empty()) {
            continue;
        }

        body += escaped_measurement;
        body += ",node=";
        body += escaped_node;
        body += ",sensor_type=";
        body += escapeMeasurementPart(sensorTypeKey(group.sensor_type));
        body += ",sensor_id=";
        body += std::to_string(group.sensor_id);
        body += " ";

        for (std::size_t i = 0; i < group.values.size(); ++i) {
            if (i > 0U) {
                body += ",";
            }
            body += escapeMeasurementPart(sensorValueKindKey(group.values[i].first));
            body += "=";
            body += formatSensorValue(group.values[i].first, group.values[i].second);
        }

        body += " ";
        body += std::to_string(group.sample_time_ms * 1000000ULL);
        body += "\n";
    }

    return body;
}

bool appendBasicAuthHeader(
    std::string_view username,
    std::string_view password,
    UploadRequestSpec& request,
    std::string& error) {
    if (username.empty() && password.empty()) {
        return true;
    }

    const std::string raw = std::string(username) + ":" + std::string(password);
    std::string encoded;
    encoded.resize(((raw.size() + 2U) / 3U) * 4U + 1U);
    size_t encoded_length = 0U;
    const int result = mbedtls_base64_encode(
        reinterpret_cast<unsigned char*>(encoded.data()),
        encoded.size(),
        &encoded_length,
        reinterpret_cast<const unsigned char*>(raw.data()),
        raw.size());
    if (result != 0) {
        error = "Failed to encode InfluxDB basic auth header.";
        return false;
    }

    encoded.resize(encoded_length);
    request.headers.push_back({"Authorization", std::string("Basic ") + encoded});
    return true;
}

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

BackendType InfluxDbUploader::type() const {
    return BackendType::kInfluxDb;
}

bool InfluxDbUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (record.host[0] == '\0') {
        error = "InfluxDB host must not be empty.";
        return false;
    }
    if (record.port == 0U) {
        error = "InfluxDB port must be greater than zero.";
        return false;
    }
    if (record.influxdb_measurement[0] == '\0') {
        error = "InfluxDB measurement name must not be empty.";
        return false;
    }
    error.clear();
    return true;
}

UploadAttemptResult InfluxDbUploader::deliver(
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

        context.resetWatchdog("before influxdb upload request");
        const UploadTransportResponse response = context.http_transport->execute(request);
        context.resetWatchdog("after influxdb upload request");

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

bool InfluxDbUploader::buildRequests(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    std::vector<UploadRequestSpec>& out_requests,
    std::string& error) const {
    out_requests.clear();
    out_requests.reserve(1U);

    if (!validateConfig(record, error)) {
        return false;
    }

    if (batch.created_unix_ms <= 0) {
        error = "InfluxDB requires valid Unix time before upload.";
        return false;
    }

    if (batch.points.empty()) {
        error.clear();
        return true;
    }

    const std::string measurement =
        boundedCString(record.influxdb_measurement, kBackendMeasurementCapacity);

    UploadRequestSpec request;
    request.request_key = std::string("influxdb:") + std::to_string(batch.batch_id);
    request.method = UploadMethod::kPost;
    request.url = buildBackendUrl(record);
    request.timeout_ms = 15000;
    request.headers.push_back({"Content-Type", "text/plain; charset=utf-8"});
    request.headers.push_back({"User-Agent", std::string("air360/") + batch.project_version});

    if (record.auth.auth_type == BackendAuthType::kBasic) {
        const std::string username =
            boundedCString(record.auth.basic_username, kBackendUsernameCapacity);
        const std::string password =
            boundedCString(record.auth.basic_password, kBackendPasswordCapacity);
        if (!appendBasicAuthHeader(username, password, request, error)) {
            return false;
        }
    }

    request.body = buildLineProtocolBody(batch, measurement);
    if (request.body.empty()) {
        error.clear();
        return true;
    }

    out_requests.push_back(std::move(request));
    error.clear();
    return true;
}

UploadResultClass InfluxDbUploader::classifyResponse(
    const UploadTransportResponse& response) const {
    if (response.transport_err != ESP_OK) {
        return UploadResultClass::kTransportError;
    }

    if (response.http_status >= 200 && response.http_status <= 208) {
        return UploadResultClass::kSuccess;
    }

    return UploadResultClass::kHttpError;
}

std::unique_ptr<IBackendUploader> createInfluxDbUploader() {
    return std::make_unique<InfluxDbUploader>();
}

}  // namespace air360
