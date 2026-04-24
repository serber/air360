#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "air360/string_utils.hpp"
#include "air360/uploads/backend_types.hpp"

namespace air360 {

constexpr std::uint32_t kBackendConfigMagic = 0x41333632U;
constexpr std::uint16_t kBackendConfigSchemaVersion = 2U;
constexpr std::uint32_t kDefaultUploadIntervalMs = 145000U;

struct BackendAuthConfig {
    BackendAuthType auth_type = BackendAuthType::kNone;
    std::uint8_t reserved[3]{};
    char basic_username[kBackendUsernameCapacity]{};
    char basic_password[kBackendPasswordCapacity]{};
};

struct BackendRecord {
    // ── header ──────────────────────────────────────────
    std::uint32_t   id           = 0U;
    std::uint8_t    enabled      = 0U;
    BackendType     backend_type = BackendType::kUnknown;
    std::uint16_t   reserved0    = 0U;

    // ── display ─────────────────────────────────────────
    char display_name[kBackendDisplayNameCapacity]{};

    // ── HTTP endpoint (common) ───────────────────────────
    char            host[kBackendHostCapacity]{};
    char            path[kBackendPathCapacity]{};
    std::uint16_t   port         = 0U;
    BackendProtocol protocol     = BackendProtocol::kHttps;
    std::uint8_t    reserved1    = 0U;

    // ── auth (common) ────────────────────────────────────
    BackendAuthConfig auth{};

    // ── SensorCommunity-specific ─────────────────────────
    char sensor_community_device_id[kBackendIdentifierCapacity]{};

    // ── InfluxDB-specific ────────────────────────────────
    char influxdb_measurement[kBackendMeasurementCapacity]{};

    // ── reserved ─────────────────────────────────────────
    std::uint8_t reserved2[8]{};
};

struct BackendConfigList {
    std::uint32_t magic          = kBackendConfigMagic;
    std::uint16_t schema_version = kBackendConfigSchemaVersion;
    std::uint16_t record_size    = static_cast<std::uint16_t>(sizeof(BackendRecord));
    std::uint16_t backend_count  = 0U;
    std::uint16_t reserved0      = 0U;
    std::uint32_t next_backend_id   = 1U;
    std::uint32_t upload_interval_ms = kDefaultUploadIntervalMs;
    std::array<BackendRecord, kMaxConfiguredBackends> backends{};
};

BackendConfigList makeDefaultBackendConfigList();

inline constexpr std::uint16_t defaultBackendPort(BackendProtocol protocol) {
    switch (protocol) {
        case BackendProtocol::kHttp:  return 80U;
        case BackendProtocol::kHttps: return 443U;
        default:                      return 0U;
    }
}

inline constexpr bool isDefaultBackendPort(
    BackendProtocol protocol,
    std::uint16_t port) {
    return port != 0U && port == defaultBackendPort(protocol);
}

inline BackendRecord* findBackendRecordByType(BackendConfigList& config, BackendType type) {
    for (std::size_t i = 0; i < config.backend_count; ++i) {
        if (config.backends[i].backend_type == type) {
            return &config.backends[i];
        }
    }
    return nullptr;
}

inline const BackendRecord* findBackendRecordByType(
    const BackendConfigList& config, BackendType type) {
    for (std::size_t i = 0; i < config.backend_count; ++i) {
        if (config.backends[i].backend_type == type) {
            return &config.backends[i];
        }
    }
    return nullptr;
}

inline std::string buildBackendUrl(const BackendRecord& record) {
    std::string url = backendProtocolScheme(record.protocol);
    url += boundedCString(record.host, kBackendHostCapacity);
    if (record.port != 0U && !isDefaultBackendPort(record.protocol, record.port)) {
        url += ':';
        url += std::to_string(record.port);
    }
    url += boundedCString(record.path, kBackendPathCapacity);
    return url;
}

inline std::string formatBackendDisplayEndpoint(const BackendRecord& record) {
    const std::string host = boundedCString(record.host, kBackendHostCapacity);
    if (host.empty()) {
        return "";
    }
    std::string ep = host;
    if (!isDefaultBackendPort(record.protocol, record.port) && record.port != 0U) {
        ep += ':';
        ep += std::to_string(record.port);
    }
    ep += boundedCString(record.path, kBackendPathCapacity);
    return ep;
}

}  // namespace air360
