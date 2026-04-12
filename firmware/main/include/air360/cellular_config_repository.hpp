#pragma once

#include <cstdint>

#include "esp_err.h"

namespace air360 {

constexpr std::uint32_t kCellularConfigMagic = 0x43454C4CU;  // "CELL"
constexpr std::uint16_t kCellularConfigSchemaVersion = 1U;

// Stored as NVS blob "cellular_cfg" in the "air360" namespace.
// Independent of DeviceConfig — versioned separately.
//
// GPIO fields use 0xFF to mean "not wired / not used".
// String fields are null-terminated; empty string means "not configured".
struct CellularConfig {
    std::uint32_t magic;
    std::uint16_t schema_version;
    std::uint16_t record_size;
    // --- control flags and hardware ---
    std::uint8_t  enabled;           // 0 = disabled; non-zero = cellular uplink active
    std::uint8_t  uart_port;         // UART port number (1 or 2)
    std::uint8_t  uart_rx_gpio;      // ESP32 RX pin (receives from modem TX)
    std::uint8_t  uart_tx_gpio;      // ESP32 TX pin (transmits to modem RX)
    std::uint32_t uart_baud;         // default 115200
    std::uint8_t  pwrkey_gpio;       // 0xFF = not wired
    std::uint8_t  sleep_gpio;        // 0xFF = not wired; drives modem DTR/sleep
    std::uint8_t  reset_gpio;        // 0xFF = not wired
    std::uint8_t  reserved0;         // padding
    std::uint16_t wifi_debug_window_s;  // seconds Wi-Fi stays up alongside cellular; 0 = disabled
    std::uint16_t reserved1;            // padding
    // --- carrier provisioning ---
    char apn[64];                    // PDP context APN; required when enabled
    char username[32];               // optional; empty if not required by carrier
    char password[64];               // optional; empty if not required by carrier
    char sim_pin[8];                 // optional; empty if SIM has no PIN lock
    // --- connectivity check ---
    char connectivity_check_host[64];  // ICMP ping target; empty = skip check
};

CellularConfig makeDefaultCellularConfig();

class CellularConfigRepository {
  public:
    esp_err_t loadOrCreate(
        CellularConfig& out_config,
        bool& loaded_from_storage,
        bool& wrote_defaults);
    esp_err_t save(const CellularConfig& config);

  private:
    bool isValid(const CellularConfig& config) const;
};

}  // namespace air360
