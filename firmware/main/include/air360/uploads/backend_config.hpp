#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "air360/string_utils.hpp"
#include "air360/uploads/backend_types.hpp"

namespace air360 {

constexpr std::uint32_t kBackendConfigMagic = 0x41333632U;
constexpr std::uint16_t kBackendConfigSchemaVersion = 1U;
constexpr std::size_t kOpenSenseMapBoxIdLength = 24U;
constexpr std::uint32_t kMinUploadIntervalMs = 30000U;
constexpr std::uint32_t kMaxUploadIntervalMs = 3600000U;
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

    // ── InfluxDB-specific ────────────────────────────────
    char influxdb_measurement[kBackendMeasurementCapacity]{};

    // ── Air360 API-specific ──────────────────────────────
    float latitude   = 0.0F;
    float longitude  = 0.0F;
    float altitude_m = 0.0F;

    // ── OpenSenseMap-specific (schema v2) ────────────────
    char opensensemap_sensebox_id[kBackendSenseBoxIdCapacity]{};
    char opensensemap_access_token[kBackendAccessTokenCapacity]{};
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

// openSenseMap box IDs come in two shapes: the classic platform uses
// 24-character hex MongoDB ObjectIds; the next-generation platform
// (staging.opensensemap.org) uses ~24-character alphanumeric cuid strings.
// Accept any alphanumeric ID of a plausible length so both work.
[[nodiscard]] inline bool isValidOpenSenseMapBoxId(std::string_view value) {
    if (value.size() < 16U || value.size() >= kBackendSenseBoxIdCapacity) {
        return false;
    }
    for (const char ch : value) {
        const bool is_alnum = (ch >= '0' && ch <= '9') ||
                              (ch >= 'a' && ch <= 'z') ||
                              (ch >= 'A' && ch <= 'Z');
        if (!is_alnum) {
            return false;
        }
    }
    return true;
}

// The box access token travels in HTTP headers; accept any printable
// non-space ASCII so future server-side token formats keep working.
[[nodiscard]] inline bool isValidOpenSenseMapAccessToken(std::string_view value) {
    if (value.size() >= kBackendAccessTokenCapacity) {
        return false;
    }
    for (const char ch : value) {
        if (ch <= 0x20 || ch >= 0x7F) {
            return false;
        }
    }
    return true;
}

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
