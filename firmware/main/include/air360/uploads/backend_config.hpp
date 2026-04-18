#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "air360/uploads/backend_types.hpp"

namespace air360 {

constexpr std::uint32_t kBackendConfigMagic = 0x41333632U;
constexpr std::uint16_t kBackendConfigSchemaVersion = 1U;
constexpr std::uint32_t kDefaultUploadIntervalMs = 145000U;

inline const char* backendDefaultEndpointUrl(BackendType type) {
    switch (type) {
        case BackendType::kSensorCommunity:
            return "https://api.sensor.community/v1/push-sensor-data/";
        case BackendType::kAir360Api:
            return "https://api.air360.ru/v1/devices/{chip_id}/batches/{batch_id}";
        case BackendType::kUnknown:
        default:
            return "";
    }
}

struct BackendRecord {
    std::uint32_t id = 0U;
    std::uint8_t enabled = 0U;
    BackendType backend_type = BackendType::kUnknown;
    std::uint16_t reserved0 = 0U;
    char display_name[kBackendDisplayNameCapacity]{};
    char device_id_override[kBackendIdentifierCapacity]{};
    char endpoint_url[kBackendUrlCapacity]{};
    char bearer_token[kBackendTokenCapacity]{};
    std::uint8_t reserved1[8]{};
};

struct BackendConfigList {
    std::uint32_t magic = kBackendConfigMagic;
    std::uint16_t schema_version = kBackendConfigSchemaVersion;
    std::uint16_t record_size = static_cast<std::uint16_t>(sizeof(BackendRecord));
    std::uint16_t backend_count = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t next_backend_id = 1U;
    std::uint32_t upload_interval_ms = kDefaultUploadIntervalMs;
    std::array<BackendRecord, kMaxConfiguredBackends> backends{};
};

BackendConfigList makeDefaultBackendConfigList();

inline void applyBackendStaticDefaults(BackendRecord& record) {
    const char* endpoint = backendDefaultEndpointUrl(record.backend_type);
    std::size_t index = 0U;
    for (; index + 1U < kBackendUrlCapacity && endpoint[index] != '\0'; ++index) {
        record.endpoint_url[index] = endpoint[index];
    }
    record.endpoint_url[index] = '\0';
    for (++index; index < kBackendUrlCapacity; ++index) {
        record.endpoint_url[index] = '\0';
    }
}

inline BackendRecord* findBackendRecordByType(BackendConfigList& config, BackendType type) {
    for (std::size_t index = 0; index < config.backend_count; ++index) {
        if (config.backends[index].backend_type == type) {
            return &config.backends[index];
        }
    }
    return nullptr;
}

inline const BackendRecord* findBackendRecordByType(
    const BackendConfigList& config,
    BackendType type) {
    for (std::size_t index = 0; index < config.backend_count; ++index) {
        if (config.backends[index].backend_type == type) {
            return &config.backends[index];
        }
    }
    return nullptr;
}

}  // namespace air360
