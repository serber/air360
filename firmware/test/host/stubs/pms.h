#pragma once
// Host model of the petrovgp/esp-pms component API used by the PMSX003
// driver. The component itself is downloaded at ESP-IDF build time and is not
// available to host tests. This model follows the component's contract for
// PMS5003: 32-byte frames, start bytes 0x42 0x4D, big-endian 16-bit sum
// checksum over bytes 0..29 stored in bytes 30..31, and field N stored
// big-endian at offset 4 + 2N.
#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

typedef enum { PMS_TYPE_1003 = 0, PMS_TYPE_3003, PMS_TYPE_5003, PMS_TYPE_MAX = 8 } pms_type_e;
typedef enum {
    PMS_STATE_SLEEP = 0,
    PMS_STATE_RESETTING,
    PMS_STATE_STABILIZING,
    PMS_STATE_ACTIVE,
    PMS_STATE_MAX
} pms_state_e;
typedef enum {
    PMS_FIELD_PM1_CF1 = 0,
    PMS_FIELD_PM2_5_CF1,
    PMS_FIELD_PM10_CF1,
    PMS_FIELD_PM1_ATM,
    PMS_FIELD_PM2_5_ATM,
    PMS_FIELD_PM10_ATM,
    PMS_FIELD_PC_0_3,
    PMS_FIELD_PC_0_5,
    PMS_FIELD_PC_1_0,
    PMS_FIELD_PC_2_5,
    PMS_FIELD_PC_5_0,
    PMS_FIELD_PC_10,
    PMS_FIELD_TEMP,
    PMS_FIELD_HUMIDITY,
    PMS_FIELD_MAX
} pms_field_t;

typedef struct {
    pms_type_e type;
    gpio_num_t set_gpio;
    gpio_num_t reset_gpio;
    uart_port_t uart_port;
} pms_config_t;

namespace pms_test {
inline pms_state_e state = PMS_STATE_ACTIVE;
inline int init_calls = 0;
inline int deinit_calls = 0;
inline std::uint8_t raw[32] = {};
}  // namespace pms_test

inline esp_err_t pms_init(pms_config_t*) {
    ++pms_test::init_calls;
    return ESP_OK;
}
inline esp_err_t pms_deinit(void) {
    ++pms_test::deinit_calls;
    return ESP_OK;
}
inline pms_state_e pms_get_state(void) {
    return pms_test::state;
}
inline esp_err_t pms_parse_data(const std::uint8_t* data, std::uint8_t len) {
    if (data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (data[0] != 0x42U || data[1] != 0x4DU) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (len != 32U) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::uint16_t sum = 0U;
    for (std::uint8_t index = 0U; index < 30U; ++index) {
        sum = static_cast<std::uint16_t>(sum + data[index]);
    }
    const std::uint16_t expected = static_cast<std::uint16_t>((data[30] << 8U) | data[31]);
    if (sum != expected) {
        return ESP_ERR_INVALID_CRC;
    }
    std::memcpy(pms_test::raw, data, 32U);
    return ESP_OK;
}
inline std::int16_t pms_get_data(pms_field_t field) {
    const std::size_t offset = 4U + 2U * static_cast<std::size_t>(field);
    return static_cast<std::int16_t>((pms_test::raw[offset] << 8U) | pms_test::raw[offset + 1U]);
}
