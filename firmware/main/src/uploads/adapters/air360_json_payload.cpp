#include "air360/uploads/adapters/air360_json_payload.hpp"

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
        case SensorType::kMhz19b:
            return "mhz19b";
        case SensorType::kUnknown:
        default:
            return "unknown";
    }
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

std::vector<SampleGroup> collectGroups(const MeasurementBatch& batch) {
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

    return groups;
}

}  // namespace

bool validateAir360JsonBatch(const MeasurementBatch& batch, std::string& error) {
    if (batch.created_unix_ms <= 0) {
        error = "Air360 JSON requires valid Unix time before upload.";
        return false;
    }

    if (batch.chip_id.empty() && batch.short_chip_id.empty()) {
        error = "Air360 JSON requires a valid chip id.";
        return false;
    }

    error.clear();
    return true;
}

std::string buildAir360JsonBody(const MeasurementBatch& batch) {
    const std::vector<SampleGroup> groups = collectGroups(batch);

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

}  // namespace air360
