#include "air360/uploads/adapters/influxdb_uploader.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensors/sensor_types.hpp"
#include "air360/uploads/backend_http_config.hpp"
#include "mbedtls/base64.h"

namespace air360 {

namespace {

struct InfluxSampleGroup {
    std::uint32_t sensor_id = 0U;
    SensorType sensor_type = SensorType::kUnknown;
    std::uint64_t sample_time_ms = 0U;
    std::vector<std::pair<SensorValueKind, float>> values;
};

const char* sensorTypeKey(SensorType type) {
    switch (type) {
        case SensorType::kBme280:
            return "bme280";
        case SensorType::kGpsNmea:
            return "gps_nmea";
        case SensorType::kDht11:
            return "dht11";
        case SensorType::kDht22:
            return "dht22";
        case SensorType::kDs18b20:
            return "ds18b20";
        case SensorType::kBme680:
            return "bme680";
        case SensorType::kSps30:
            return "sps30";
        case SensorType::kScd30:
            return "scd30";
        case SensorType::kHtu2x:
            return "htu2x";
        case SensorType::kSht4x:
            return "sht4x";
        case SensorType::kMe3No2:
            return "me3_no2";
        case SensorType::kVeml7700:
            return "veml7700";
        case SensorType::kIna219:
            return "ina219";
        case SensorType::kMhz19b:
            return "mhz19b";
        case SensorType::kUnknown:
        default:
            return "unknown";
    }
}

std::string formatNumericValue(SensorValueKind kind, float value) {
    char buffer[48];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.*f",
        sensorValueKindPrecision(kind),
        static_cast<double>(value));
    return buffer;
}

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

void upsertLatestValue(
    InfluxSampleGroup& group,
    SensorValueKind kind,
    float value) {
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
    const BackendHttpConfigView& config) {
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
    const std::string escaped_measurement = escapeMeasurementPart(config.measurement_name);
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

        for (std::size_t index = 0; index < group.values.size(); ++index) {
            if (index > 0U) {
                body += ",";
            }
            body += escapeMeasurementPart(sensorValueKindKey(group.values[index].first));
            body += "=";
            body += formatNumericValue(group.values[index].first, group.values[index].second);
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

}  // namespace

BackendType InfluxDbUploader::type() const {
    return BackendType::kInfluxDb;
}

const char* InfluxDbUploader::backendKey() const {
    return "influxdb";
}

bool InfluxDbUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    return validateBackendHttpRecord(record, error);
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

    BackendHttpConfigView config;
    if (!decodeBackendHttpRecord(record, config, error)) {
        return false;
    }
    std::string endpoint_url;
    if (!buildBackendHttpUrl(config, endpoint_url, error)) {
        return false;
    }

    UploadRequestSpec request;
    request.request_key = std::string("influxdb:") + std::to_string(batch.batch_id);
    request.method = UploadMethod::kPost;
    request.url = endpoint_url;
    request.timeout_ms = 15000;
    request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    request.headers.push_back({"User-Agent", std::string("air360/") + batch.project_version});
    if (!appendBasicAuthHeader(config.username, config.password, request, error)) {
        return false;
    }
    request.body = buildLineProtocolBody(batch, config);
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
