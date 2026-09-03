#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "esp_err.h"

namespace air360 {

constexpr std::uint32_t kDeviceConfigMagic = 0x41333630U;
constexpr std::uint16_t kDeviceConfigSchemaVersion = 2U;
// Byte size of the schema v1 blob. Kept only so loadOrCreate() can migrate a
// stored v1 record in place instead of resetting it to defaults.
constexpr std::size_t kDeviceConfigV1Size = 376U;

// Boot-time power gate (schema v2). The gate compares the INA219/INA226 bus
// voltage measured before any radio is powered against power_gate_threshold_mv
// and deep-sleeps instead of continuing boot while the supply is below it.
constexpr std::uint16_t kPowerGateThresholdMinMv = 1000U;
constexpr std::uint16_t kPowerGateThresholdMaxMv = 36000U;   // INA226 bus range
constexpr std::uint16_t kPowerGateDefaultThresholdMv = 4700U; // sagging 5 V rail
constexpr std::uint16_t kPowerGateSleepMinS = 30U;
constexpr std::uint16_t kPowerGateSleepMaxS = 7200U;
constexpr std::uint16_t kPowerGateDefaultSleepBaseS = 300U;
constexpr std::uint16_t kPowerGateDefaultSleepMaxS = 1800U;
constexpr std::uint16_t kPowerGateSampleWaitMinS = 5U;
constexpr std::uint16_t kPowerGateSampleWaitMaxS = 120U;
constexpr std::uint16_t kPowerGateDefaultSampleWaitS = 15U;

constexpr std::uint8_t kBleAdvIntervalCount = 4U;
constexpr std::uint16_t kBleAdvIntervalTable[kBleAdvIntervalCount] = {100U, 300U, 1000U, 3000U};
constexpr std::uint8_t kBleAdvIntervalDefaultIndex = 2U;  // 1000 ms

struct DeviceConfig {
    std::uint32_t magic;
    std::uint16_t schema_version;
    std::uint16_t record_size;
    std::uint16_t http_port;
    std::uint8_t lab_ap_enabled;
    std::uint8_t local_auth_enabled;
    std::uint8_t wifi_power_save_enabled;
    std::uint8_t ble_advertise_enabled;   // was reserved0; 0=off, 1=on
    char device_name[32];
    char wifi_sta_ssid[33];
    char wifi_sta_password[65];
    char lab_ap_ssid[33];
    char lab_ap_password[65];
    char sntp_server[64];
    std::uint8_t sta_use_static_ip;
    std::uint8_t ble_adv_interval_index;  // was reserved1[0]; index into kBleAdvIntervalTable
    std::uint8_t reserved1[2];
    char sta_ip[16];
    char sta_netmask[16];
    char sta_gateway[16];
    char sta_dns[16];
    // ---- schema v2 ----
    std::uint8_t power_gate_enabled;        // 0 = off, 1 = gate boot on bus voltage
    std::uint8_t reserved2;
    std::uint16_t power_gate_threshold_mv;  // continue boot only when voltage >= this
    std::uint16_t power_gate_sleep_base_s;  // first deep-sleep duration below threshold
    std::uint16_t power_gate_sleep_max_s;   // cap for the escalating sleep duration
    std::uint16_t power_gate_sample_wait_s; // max wait for the first voltage sample
};

// The v2 fields are appended after the last v1 field, so a stored v1 blob is a
// byte-exact prefix of the v2 struct and can be migrated by copying it in.
static_assert(
    offsetof(DeviceConfig, power_gate_enabled) + 2U == kDeviceConfigV1Size,
    "DeviceConfig v2 fields must start where the v1 tail padding was");
static_assert(sizeof(DeviceConfig) == 384U, "DeviceConfig layout changed; bump schema");

DeviceConfig makeDefaultDeviceConfig();
// Resets only the schema v2 power-gate fields to their defaults.
void applyPowerGateDefaults(DeviceConfig& config);
// Range-checks the power-gate fields; out_error names the first violation.
bool validatePowerGateConfig(const DeviceConfig& config, const char*& out_error);
bool isValidIpv4Address(std::string_view value);
bool validateStaticIpv4Config(
    bool sta_use_static_ip,
    std::string_view sta_ip,
    std::string_view sta_netmask,
    std::string_view sta_gateway,
    std::string_view sta_dns,
    const char*& out_error);
bool validateStaticIpv4Config(const DeviceConfig& config, const char*& out_error);

class ConfigRepository {
  public:
    [[nodiscard]] esp_err_t loadOrCreate(
        DeviceConfig& out_config,
        bool& loaded_from_storage,
        bool& wrote_defaults);
    [[nodiscard]] esp_err_t save(const DeviceConfig& config);
    [[nodiscard]] esp_err_t incrementBootCount(std::uint32_t& out_boot_count);
    [[nodiscard]] bool isValid(const DeviceConfig& config) const;
};

}  // namespace air360
