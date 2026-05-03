#include "air360/uploads/adapters/sensor_community_uploader.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensor_format_utils.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/string_utils.hpp"
#include "air360/uploads/backend_config.hpp"
#include "air360/uploads/upload_transport.hpp"

namespace air360 {

namespace {

constexpr char kLegacyPrefix[] = "esp32-";

std::string legacyDeviceId(const MeasurementBatch& batch) {
    if (!batch.short_device_id.empty()) {
        return batch.short_device_id;
    }
    return batch.device_id;
}

std::string overrideDeviceId(const BackendRecord& record) {
    return boundedCString(record.sensor_community_device_id, kBackendIdentifierCapacity);
}

bool mapMeasurement(
    const MeasurementPoint& point,
    std::uint8_t& out_pin,
    const char*& out_value_type) {
    switch (point.sensor_type) {
        case SensorType::kBme280:
        case SensorType::kBme680:
            out_pin = 11U;
            switch (point.value_kind) {
                case SensorValueKind::kTemperatureC:
                    out_value_type = "temperature";
                    return true;
                case SensorValueKind::kPressureHpa:
                    out_value_type = "pressure";
                    return true;
                case SensorValueKind::kHumidityPercent:
                    out_value_type = "humidity";
                    return true;
                default:
                    return false;
            }
        case SensorType::kDht11:
        case SensorType::kDht22:
        case SensorType::kHtu2x:
        case SensorType::kSht3x:
        case SensorType::kSht4x:
        case SensorType::kDs18b20:
            out_pin = 7U;
            switch (point.value_kind) {
                case SensorValueKind::kTemperatureC:
                    out_value_type = "temperature";
                    return true;
                case SensorValueKind::kHumidityPercent:
                    out_value_type = "humidity";
                    return true;
                default:
                    return false;
            }
        case SensorType::kScd30:
            out_pin = 17U;
            switch (point.value_kind) {
                case SensorValueKind::kTemperatureC:
                    out_value_type = "temperature";
                    return true;
                case SensorValueKind::kHumidityPercent:
                    out_value_type = "humidity";
                    return true;
                case SensorValueKind::kCo2Ppm:
                    out_value_type = "co2_ppm";
                    return true;
                default:
                    return false;
            }
        case SensorType::kGpsNmea:
            out_pin = 9U;
            switch (point.value_kind) {
                case SensorValueKind::kLatitudeDeg:
                    out_value_type = "lat";
                    return true;
                case SensorValueKind::kLongitudeDeg:
                    out_value_type = "lon";
                    return true;
                case SensorValueKind::kAltitudeM:
                    out_value_type = "height";
                    return true;
                default:
                    return false;
            }
        case SensorType::kSps30:
            out_pin = 1U;
            switch (point.value_kind) {
                case SensorValueKind::kPm1_0UgM3:
                    out_value_type = "P0";
                    return true;
                case SensorValueKind::kPm2_5UgM3:
                    out_value_type = "P2";
                    return true;
                case SensorValueKind::kPm4_0UgM3:
                    out_value_type = "P4";
                    return true;
                case SensorValueKind::kPm10_0UgM3:
                    out_value_type = "P1";
                    return true;
                case SensorValueKind::kNc0_5PerCm3:
                    out_value_type = "N05";
                    return true;
                case SensorValueKind::kNc1_0PerCm3:
                    out_value_type = "N1";
                    return true;
                case SensorValueKind::kNc2_5PerCm3:
                    out_value_type = "N25";
                    return true;
                case SensorValueKind::kNc4_0PerCm3:
                    out_value_type = "N4";
                    return true;
                case SensorValueKind::kNc10_0PerCm3:
                    out_value_type = "N10";
                    return true;
                case SensorValueKind::kTypicalParticleSizeUm:
                    out_value_type = "TS";
                    return true;
                default:
                    return false;
            }
        case SensorType::kSds011:
            out_pin = 1U;
            switch (point.value_kind) {
                case SensorValueKind::kPm2_5UgM3:
                    out_value_type = "P2";
                    return true;
                case SensorValueKind::kPm10_0UgM3:
                    out_value_type = "P1";
                    return true;
                default:
                    return false;
            }
        case SensorType::kUnknown:
        case SensorType::kMe3No2:
        case SensorType::kVeml7700:
        default:
            return false;
    }
}

struct SensorCommunityGroup {
    std::uint32_t sensor_id = 0U;
    std::uint8_t pin = 0U;
    std::vector<std::pair<std::string, std::string>> values;
};

SensorCommunityGroup* findGroup(
    std::vector<SensorCommunityGroup>& groups,
    std::uint32_t sensor_id,
    std::uint8_t pin) {
    for (auto& group : groups) {
        if (group.sensor_id == sensor_id && group.pin == pin) {
            return &group;
        }
    }
    return nullptr;
}

void upsertLatestValue(
    SensorCommunityGroup& group,
    const char* value_type,
    SensorValueKind kind,
    float value) {
    const std::string value_type_key = value_type != nullptr ? value_type : "";
    const std::string formatted_value = formatSensorValue(kind, value);

    for (auto& entry : group.values) {
        if (entry.first == value_type_key) {
            entry.second = formatted_value;
            return;
        }
    }

    group.values.emplace_back(value_type_key, formatted_value);
}

std::string buildBody(
    const MeasurementBatch& batch,
    const SensorCommunityGroup& group) {
    std::string body;
    body += "{\"software_version\":\"";
    body += jsonEscape(batch.project_version);
    body += "\",\"sensordatavalues\":[";
    for (std::size_t index = 0; index < group.values.size(); ++index) {
        if (index > 0U) {
            body += ",";
        }
        body += "{\"value_type\":\"";
        body += jsonEscape(group.values[index].first);
        body += "\",\"value\":\"";
        body += jsonEscape(group.values[index].second);
        body += "\"}";
    }
    body += "]}";
    return body;
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

BackendType SensorCommunityUploader::type() const {
    return BackendType::kSensorCommunity;
}

bool SensorCommunityUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (record.host[0] == '\0') {
        error = "Sensor.Community host must not be empty.";
        return false;
    }
    if (record.port == 0U) {
        error = "Sensor.Community port must be greater than zero.";
        return false;
    }
    error.clear();
    return true;
}

UploadAttemptResult SensorCommunityUploader::deliver(
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

        context.resetWatchdog("before sensor.community upload request");
        const UploadTransportResponse response = context.http_transport->execute(request);
        context.resetWatchdog("after sensor.community upload request");

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

bool SensorCommunityUploader::buildRequests(
    const BackendRecord& record,
    const MeasurementBatch& batch,
    std::vector<UploadRequestSpec>& out_requests,
    std::string& error) const {
    out_requests.clear();

    if (!validateConfig(record, error)) {
        return false;
    }

    std::vector<SensorCommunityGroup> groups;
    for (const auto& point : batch.points) {
        std::uint8_t pin = 0U;
        const char* value_type = nullptr;
        if (!mapMeasurement(point, pin, value_type)) {
            continue;
        }

        SensorCommunityGroup* group = findGroup(groups, point.sensor_id, pin);
        if (group == nullptr) {
            groups.push_back(SensorCommunityGroup{point.sensor_id, pin, {}});
            group = &groups.back();
        }

        upsertLatestValue(*group, value_type, point.value_kind, point.value);
    }

    const std::string device_id_override = overrideDeviceId(record);
    const std::string device_id = device_id_override.empty() ? legacyDeviceId(batch) : device_id_override;
    const std::string x_sensor = std::string(kLegacyPrefix) + device_id;
    const std::string x_mac = std::string(kLegacyPrefix) + batch.esp_mac_id;
    const std::string user_agent =
        batch.project_version + "/" + device_id + "/" + batch.esp_mac_id;
    const std::string endpoint_url = buildBackendUrl(record);

    for (const auto& group : groups) {
        if (group.values.empty()) {
            continue;
        }

        UploadRequestSpec request;
        request.request_key =
            std::string("sensor_community:") + std::to_string(group.sensor_id);
        request.url = endpoint_url;
        request.timeout_ms = 15000;
        request.headers.push_back({"Content-Type", "application/json"});
        request.headers.push_back({"X-Sensor", x_sensor});
        request.headers.push_back({"X-MAC-ID", x_mac});
        request.headers.push_back({"X-PIN", std::to_string(group.pin)});
        request.headers.push_back({"User-Agent", user_agent});
        request.body = buildBody(batch, group);
        out_requests.push_back(std::move(request));
    }

    error.clear();
    return true;
}

UploadResultClass SensorCommunityUploader::classifyResponse(
    const UploadTransportResponse& response) const {
    if (response.transport_err != ESP_OK) {
        return UploadResultClass::kTransportError;
    }

    if (response.http_status >= 200 && response.http_status <= 208) {
        return UploadResultClass::kSuccess;
    }

    return UploadResultClass::kHttpError;
}

std::unique_ptr<IBackendUploader> createSensorCommunityUploader() {
    return std::make_unique<SensorCommunityUploader>();
}

}  // namespace air360
