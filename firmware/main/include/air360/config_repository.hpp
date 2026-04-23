#pragma once

#include <cstdint>
#include <string_view>

#include "esp_err.h"

namespace air360 {

constexpr std::uint32_t kDeviceConfigMagic = 0x41333630U;
constexpr std::uint16_t kDeviceConfigSchemaVersion = 1U;

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
};

DeviceConfig makeDefaultDeviceConfig();
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

  private:
    bool isValid(const DeviceConfig& config) const;
};

}  // namespace air360
