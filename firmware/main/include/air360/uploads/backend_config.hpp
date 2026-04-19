#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "air360/uploads/backend_types.hpp"

namespace air360 {

constexpr std::uint32_t kBackendConfigMagic = 0x41333632U;
constexpr std::uint16_t kBackendConfigSchemaVersion = 1U;
constexpr std::uint32_t kDefaultUploadIntervalMs = 145000U;

inline const char* backendDefaultHost(BackendType type) {
    switch (type) {
        case BackendType::kSensorCommunity:
            return "api.sensor.community";
        case BackendType::kAir360Api:
            return "api.air360.ru";
        case BackendType::kCustomUpload:
        case BackendType::kInfluxDb:
        case BackendType::kUnknown:
        default:
            return "";
    }
}

inline const char* backendDefaultPath(BackendType type) {
    switch (type) {
        case BackendType::kSensorCommunity:
            return "/v1/push-sensor-data/";
        case BackendType::kAir360Api:
            return "/v1/devices/{chip_id}/batches/{batch_id}";
        case BackendType::kCustomUpload:
        case BackendType::kInfluxDb:
        case BackendType::kUnknown:
        default:
            return "";
    }
}

inline std::uint16_t backendDefaultPort(BackendType type) {
    switch (type) {
        case BackendType::kSensorCommunity:
        case BackendType::kAir360Api:
        case BackendType::kInfluxDb:
            return 443U;
        case BackendType::kCustomUpload:
        case BackendType::kUnknown:
        default:
            return 0U;
    }
}

inline bool backendDefaultUseHttps(BackendType type) {
    switch (type) {
        case BackendType::kSensorCommunity:
        case BackendType::kAir360Api:
        case BackendType::kInfluxDb:
            return true;
        case BackendType::kCustomUpload:
        case BackendType::kUnknown:
        default:
            return false;
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
    char host[kBackendHostCapacity]{};
    char path[kBackendPathCapacity]{};
    char username[kBackendUsernameCapacity]{};
    char password[kBackendPasswordCapacity]{};
    char measurement_name[kBackendMeasurementCapacity]{};
    std::uint16_t port = 0U;
    std::uint8_t use_https = 1U;
    std::uint8_t reserved1[5]{};
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
    record.port = backendDefaultPort(record.backend_type);
    record.use_https = backendDefaultUseHttps(record.backend_type) ? 1U : 0U;

    const char* host = backendDefaultHost(record.backend_type);
    std::size_t index = 0U;
    for (; index + 1U < kBackendHostCapacity && host[index] != '\0'; ++index) {
        record.host[index] = host[index];
    }
    record.host[index] = '\0';
    for (++index; index < kBackendHostCapacity; ++index) {
        record.host[index] = '\0';
    }

    const char* path = backendDefaultPath(record.backend_type);
    index = 0U;
    for (; index + 1U < kBackendPathCapacity && path[index] != '\0'; ++index) {
        record.path[index] = path[index];
    }
    record.path[index] = '\0';
    for (++index; index < kBackendPathCapacity; ++index) {
        record.path[index] = '\0';
    }

    if (record.backend_type == BackendType::kInfluxDb) {
        constexpr char kDefaultMeasurementName[] = "air360";
        std::size_t measurement_index = 0U;
        for (; measurement_index + 1U < kBackendMeasurementCapacity &&
               kDefaultMeasurementName[measurement_index] != '\0';
             ++measurement_index) {
            record.measurement_name[measurement_index] = kDefaultMeasurementName[measurement_index];
        }
        record.measurement_name[measurement_index] = '\0';
        for (++measurement_index; measurement_index < kBackendMeasurementCapacity;
             ++measurement_index) {
            record.measurement_name[measurement_index] = '\0';
        }
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
