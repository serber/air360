#include "air360/uploads/adapters/air360_json_payload.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "air360/sensor_format_utils.hpp"
#include "air360/sensors/sensor_types.hpp"
#include "air360/string_utils.hpp"

namespace air360 {

namespace {

struct SampleGroup {
    SensorType sensor_type = SensorType::kUnknown;
    std::uint64_t sample_time_ms = 0U;
    std::vector<std::pair<SensorValueKind, float>> values;
};

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
            body += formatSensorValue(value.first, value.second);
            body += "}";
        }
        body += "]}";
    }

    body += "]}}";
    return body;
}

}  // namespace air360
