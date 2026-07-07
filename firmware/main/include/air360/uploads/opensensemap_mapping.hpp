#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "air360/sensors/sensor_types.hpp"

namespace air360 {

// openSenseMap sensor IDs are 24-character hexadecimal MongoDB ObjectIds
// (e.g. "d5e430f89c46057f5c627ff1"). Store 24 characters plus a NUL.
constexpr std::size_t kOpenSenseMapSensorIdCapacity = 25U;

// Upper bound on how many device readings can be mapped to openSenseMap
// sensors. A device runs at most kMaxConfiguredSensors sensors; this cap is
// generous enough to cover every mappable phenomenon across them while keeping
// the persisted blob small.
constexpr std::size_t kMaxOpenSenseMapMappings = 24U;

constexpr std::uint32_t kOpenSenseMapMappingMagic = 0x4F53454DU;  // 'OSEM'
constexpr std::uint16_t kOpenSenseMapMappingSchemaVersion = 1U;

// One device reading (identified by sensor model + phenomenon) bound to a
// single openSenseMap sensor ID configured on the box.
struct OpenSenseMapMapping {
    SensorType sensor_type = SensorType::kUnknown;
    SensorValueKind value_kind = SensorValueKind::kUnknown;
    char sensor_id[kOpenSenseMapSensorIdCapacity]{};
};

// Persisted table of reading -> openSenseMap sensor bindings for the single
// openSenseMap backend. Stored as its own NVS blob rather than inside
// BackendRecord so it does not bloat the five-slot backend array.
struct OpenSenseMapMappingTable {
    std::uint32_t magic = kOpenSenseMapMappingMagic;
    std::uint16_t schema_version = kOpenSenseMapMappingSchemaVersion;
    std::uint16_t entry_count = 0U;
    std::array<OpenSenseMapMapping, kMaxOpenSenseMapMappings> entries{};
};

// openSenseMap sensor IDs are exactly 24 hex characters. Accept upper- or
// lower-case; the value is stored verbatim and only ever placed in a JSON key.
[[nodiscard]] inline bool isValidOpenSenseMapSensorId(std::string_view value) {
    if (value.size() != 24U) {
        return false;
    }
    for (const char ch : value) {
        const bool is_hex = (ch >= '0' && ch <= '9') ||
                            (ch >= 'a' && ch <= 'f') ||
                            (ch >= 'A' && ch <= 'F');
        if (!is_hex) {
            return false;
        }
    }
    return true;
}

// Returns the mapped openSenseMap sensor ID for a (sensor_type, value_kind)
// reading, or nullptr when the reading is unmapped. The returned pointer aliases
// the table entry, so it stays valid as long as the table does.
[[nodiscard]] inline const char* findOpenSenseMapSensorId(
    const OpenSenseMapMappingTable& table,
    SensorType sensor_type,
    SensorValueKind value_kind) {
    const std::size_t count = table.entry_count <= kMaxOpenSenseMapMappings
                                  ? table.entry_count
                                  : kMaxOpenSenseMapMappings;
    for (std::size_t index = 0; index < count; ++index) {
        const OpenSenseMapMapping& entry = table.entries[index];
        if (entry.sensor_type == sensor_type &&
            entry.value_kind == value_kind &&
            entry.sensor_id[0] != '\0') {
            return entry.sensor_id;
        }
    }
    return nullptr;
}

}  // namespace air360
