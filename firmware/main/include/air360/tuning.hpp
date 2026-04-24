#pragma once

#include <cstddef>
#include <cstdint>

#include "sdkconfig.h"

namespace air360::tuning {

namespace network {

// 10 s first retry: short enough to recover a transient AP reboot, long enough
// to avoid hammering a station peer that is still reassociating.
inline constexpr std::uint32_t kReconnectBaseDelayMs =
    CONFIG_AIR360_WIFI_RECONNECT_BASE_DELAY_MS;

// 5 min cap: lets long outages settle without letting reconnects disappear for
// tens of minutes after the first failure burst.
inline constexpr std::uint32_t kReconnectMaxDelayMs =
    CONFIG_AIR360_WIFI_RECONNECT_MAX_DELAY_MS;

// 3 min setup-AP retry: keeps provisioning responsive while still giving the
// upstream Wi-Fi network time to finish booting after a power cut.
inline constexpr std::uint32_t kSetupApRetryDelayMs =
    CONFIG_AIR360_WIFI_SETUP_AP_RETRY_DELAY_MS;

// 2 s suppression window: masks the disconnect events caused by our own
// esp_wifi_stop()/esp_wifi_disconnect() calls during mode changes.
inline constexpr std::uint32_t kDisconnectIgnoreWindowMs =
    CONFIG_AIR360_WIFI_DISCONNECT_IGNORE_WINDOW_MS;

// 15 s connect timeout: long enough for WPA join + DHCP on a busy AP, short
// enough that setup-AP fallback and reconnect backoff stay responsive.
inline constexpr std::uint32_t kConnectTimeoutMs =
    CONFIG_AIR360_WIFI_CONNECT_TIMEOUT_MS;

}  // namespace network

namespace upload {

// 256 queued samples stores about 7 minutes of backlog at 3 sensors / 5 s
// polling, which is enough to bridge short WAN outages without unbounded RAM
// growth on ESP32-S3.
inline constexpr std::size_t kMeasurementQueueDepth =
    CONFIG_AIR360_MEASUREMENT_QUEUE_DEPTH;

}  // namespace upload

namespace ble {

// Refreshing the BTHome payload every 5 s matches slow-changing environmental
// telemetry while avoiding needless store scans and radio payload churn.
inline constexpr std::uint32_t kPayloadRefreshIntervalMs =
    CONFIG_AIR360_BLE_PAYLOAD_REFRESH_INTERVAL_MS;

}  // namespace ble

}  // namespace air360::tuning
