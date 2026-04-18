#include "air360/uploads/adapters/air360_api_uploader.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensors/sensor_types.hpp"

namespace air360 {

namespace {

struct SampleGroup {
    SensorType sensor_type = SensorType::kUnknown;
    std::uint64_t sample_time_ms = 0U;
    std::vector<std::pair<SensorValueKind, float>> values;
};

std::string jsonEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char ch : input) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

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
        case SensorType::kUnknown:
        default:
            return "unknown";
    }
}

std::string trimTrailingSlash(const char* url) {
    std::string trimmed = url != nullptr ? url : "";
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}

std::string formatValue(SensorValueKind kind, float value) {
    char buffer[48];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.*f",
        sensorValueKindPrecision(kind),
        static_cast<double>(value));
    return buffer;
}

SampleGroup* findGroup(
    std::vector<SampleGroup>& groups,
    SensorType sensor_type,
    std::uint64_t sample_time_ms) {
    for (auto& group : groups) {
        if (group.sensor_type == sensor_type && group.sample_time_ms == sample_time_ms) {
            return &group;
        }
    }
    return nullptr;
}

std::string buildUrl(const BackendRecord& record, const MeasurementBatch& batch) {
    const std::string base = trimTrailingSlash(backendDefaultEndpointUrl(record.backend_type));
    const std::string chip_id = !batch.chip_id.empty() ? batch.chip_id : batch.short_chip_id;
    return base + "/v1/devices/" + chip_id + "/batches/" + std::to_string(batch.batch_id);
}

std::string buildBody(
    const MeasurementBatch& batch,
    const std::vector<SampleGroup>& groups) {
    std::string body;
    body.reserve(512U + groups.size() * 192U);
    body += "{\"schema_version\":1";
    body += ",\"sent_at_unix_ms\":";
    body += std::to_string(batch.created_unix_ms);
    body += ",\"device\":{";
    body += "\"device_name\":\"";
    body += jsonEscape(batch.device_name);
    body += "\",\"board_name\":\"";
    body += jsonEscape(batch.board_name);
    body += "\",\"chip_id\":\"";
    body += jsonEscape(batch.chip_id);
    body += "\",\"short_chip_id\":\"";
    body += jsonEscape(batch.short_chip_id);
    body += "\",\"esp_mac_id\":\"";
    body += jsonEscape(batch.esp_mac_id);
    body += "\",\"firmware_version\":\"";
    body += jsonEscape(batch.project_version);
    body += "\"},\"batch\":{\"sample_count\":";
    body += std::to_string(groups.size());
    body += ",\"samples\":[";

    for (std::size_t sample_index = 0; sample_index < groups.size(); ++sample_index) {
        const auto& group = groups[sample_index];
        if (sample_index > 0U) {
            body += ",";
        }
        body += "{\"sensor_type\":\"";
        body += jsonEscape(sensorTypeKey(group.sensor_type));
        body += "\",\"sample_time_unix_ms\":";
        body += std::to_string(group.sample_time_ms);
        body += ",\"values\":[";
        for (std::size_t value_index = 0; value_index < group.values.size(); ++value_index) {
            const auto& value = group.values[value_index];
            if (value_index > 0U) {
                body += ",";
            }
            body += "{\"kind\":\"";
            body += jsonEscape(sensorValueKindKey(value.first));
            body += "\",\"value\":";
            body += formatValue(value.first, value.second);
            body += "}";
        }
        body += "]}";
    }

    body += "]}}";
    return body;
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
    if (backendDefaultEndpointUrl(record.backend_type)[0] == '\0') {
        error = "Air360 API base URL is empty.";
        return false;
    }

    error.clear();
    return true;
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

    if (batch.created_unix_ms <= 0) {
        error = "Air360 API requires valid Unix time before upload.";
        return false;
    }

    if (batch.chip_id.empty() && batch.short_chip_id.empty()) {
        error = "Air360 API requires a valid chip id.";
        return false;
    }

    std::vector<SampleGroup> groups;
    groups.reserve(batch.points.size());
    for (const auto& point : batch.points) {
        SampleGroup* group = findGroup(groups, point.sensor_type, point.sample_time_ms);
        if (group == nullptr) {
            groups.push_back(SampleGroup{point.sensor_type, point.sample_time_ms, {}});
            group = &groups.back();
        }
        group->values.emplace_back(point.value_kind, point.value);
    }

    if (groups.empty()) {
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
    request.body = buildBody(batch, groups);
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
