#include "air360/uploads/adapters/sensor_community_uploader.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensors/sensor_types.hpp"

namespace air360 {

namespace {

constexpr char kLegacyPrefix[] = "esp32-";

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

std::string legacyChipId(const MeasurementBatch& batch) {
    if (!batch.short_chip_id.empty()) {
        return batch.short_chip_id;
    }
    return batch.chip_id;
}

std::string overrideChipId(const BackendRecord& record) {
    const char* raw = record.device_id_override;
    if (raw == nullptr || raw[0] == '\0') {
        return "";
    }

    std::size_t length = 0U;
    while (length < kBackendIdentifierCapacity && raw[length] != '\0') {
        ++length;
    }
    return std::string(raw, length);
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
    const std::string formatted_value = formatValue(kind, value);

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

}  // namespace

BackendType SensorCommunityUploader::type() const {
    return BackendType::kSensorCommunity;
}

const char* SensorCommunityUploader::backendKey() const {
    return "sensor_community";
}

bool SensorCommunityUploader::validateConfig(
    const BackendRecord& record,
    std::string& error) const {
    if (backendDefaultEndpointUrl(record.backend_type)[0] == '\0') {
        error = "Sensor.Community endpoint URL is empty.";
        return false;
    }

    error.clear();
    return true;
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

    const std::string chip_id_override = overrideChipId(record);
    const std::string chip_id = chip_id_override.empty() ? legacyChipId(batch) : chip_id_override;
    const std::string x_sensor = std::string(kLegacyPrefix) + chip_id;
    const std::string x_mac = std::string(kLegacyPrefix) + batch.esp_mac_id;
    const std::string user_agent =
        batch.project_version + "/" + chip_id + "/" + batch.esp_mac_id;

    for (const auto& group : groups) {
        if (group.values.empty()) {
            continue;
        }

        UploadRequestSpec request;
        request.request_key =
            std::string("sensor_community:") + std::to_string(group.sensor_id);
        request.url = backendDefaultEndpointUrl(record.backend_type);
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
