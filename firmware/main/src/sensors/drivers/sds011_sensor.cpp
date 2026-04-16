#include "air360/sensors/drivers/sds011_sensor.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

// SDS011 binary frame layout (10 bytes, active reporting mode):
//   [0]  0xAA  head
//   [1]  0xC0  command (data report)
//   [2]  PM2.5 low byte
//   [3]  PM2.5 high byte
//   [4]  PM10  low byte
//   [5]  PM10  high byte
//   [6]  device ID low byte  (ignored)
//   [7]  device ID high byte (ignored)
//   [8]  checksum = (byte[2]+byte[3]+byte[4]+byte[5]+byte[6]+byte[7]) & 0xFF
//   [9]  0xAB  tail
//
// Concentration (µg/m³) = uint16_le(high, low) / 10.0

constexpr char kTag[] = "air360.sds011";

constexpr std::uint8_t kFrameHead = 0xAAU;
constexpr std::uint8_t kFrameCmd  = 0xC0U;
constexpr std::uint8_t kFrameTail = 0xABU;
constexpr std::size_t  kFrameLen  = 10U;

// Number of bytes to request per read call.  At 1 Hz / 10 bytes per frame the
// UART buffer accumulates ~10 bytes/s; 128 bytes covers a 12-second backlog
// with room to spare.
constexpr std::size_t kReadBufLen = 128U;
constexpr TickType_t  kWakeDelayTicks = pdMS_TO_TICKS(250U);
constexpr TickType_t  kModeDelayTicks = pdMS_TO_TICKS(100U);

// SDS011 control command frame (19 bytes):
// [0]  0xAA  head
// [1]  0xB4  command id
// [2]  sub-command byte 1
// [3]  sub-command byte 2 (set=0x01 / query=0x00)
// [4]  sub-command byte 3 (data value)
// [5..14] 0x00  reserved
// [15] 0xFF  device ID low  (0xFFFF = broadcast)
// [16] 0xFF  device ID high
// [17] checksum = (byte[2]+byte[3]+byte[4] + 0xFF+0xFF) & 0xFF
//              = (byte[2]+byte[3]+byte[4] - 2) & 0xFF
// [18] 0xAB  tail

// Wake up: set working state = 1 (working/awake).
// checksum = (0x06 + 0x01 + 0x01 - 2) & 0xFF = 0x06
constexpr std::uint8_t kWakeupCmd[19] = {
    0xAAU, 0xB4U, 0x06U, 0x01U, 0x01U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0xFFU, 0xFFU, 0x06U, 0xABU
};
// Set work period = 0 (continuous output, not periodic).
// checksum = (0x08 + 0x01 + 0x00 - 2) & 0xFF = 0x07
constexpr std::uint8_t kWorkPeriodCmd[19] = {
    0xAAU, 0xB4U, 0x08U, 0x01U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0xFFU, 0xFFU, 0x07U, 0xABU
};
// Set reporting mode = active (sensor pushes one frame per second).
// checksum = (0x02 + 0x01 + 0x00 - 2) & 0xFF = 0x01
constexpr std::uint8_t kActiveModeCmd[19] = {
    0xAAU, 0xB4U, 0x02U, 0x01U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0xFFU, 0xFFU, 0x01U, 0xABU
};

bool writeFullCommand(
    UartPortManager* uart_port_manager,
    std::uint8_t port_id,
    const std::uint8_t* data,
    std::size_t size,
    const char* label) {
    const int written = uart_port_manager->write(port_id, data, size);
    if (written != static_cast<int>(size)) {
        ESP_LOGW(
            kTag,
            "SDS011 %s command write incomplete: wrote %d of %u bytes",
            label,
            written,
            static_cast<unsigned>(size));
        return false;
    }
    return true;
}

bool configureActiveMode(UartPortManager* uart_port_manager, std::uint8_t port_id) {
    uart_port_manager->flush(port_id);

    bool ok = writeFullCommand(
        uart_port_manager, port_id, kWakeupCmd, sizeof(kWakeupCmd), "wake");
    vTaskDelay(kWakeDelayTicks);
    ok = writeFullCommand(
             uart_port_manager, port_id, kWorkPeriodCmd, sizeof(kWorkPeriodCmd), "work-period") &&
         ok;
    vTaskDelay(kModeDelayTicks);
    ok = writeFullCommand(
             uart_port_manager, port_id, kActiveModeCmd, sizeof(kActiveModeCmd), "active-mode") &&
         ok;
    vTaskDelay(kModeDelayTicks);
    uart_port_manager->flush(port_id);
    return ok;
}

// Validate and decode one 10-byte frame.  Returns true on success.
bool decodeFrame(const std::uint8_t* f, float& pm25, float& pm10) {
    if (f[0] != kFrameHead || f[1] != kFrameCmd || f[9] != kFrameTail) {
        return false;
    }

    const std::uint8_t cs = static_cast<std::uint8_t>(
        f[2] + f[3] + f[4] + f[5] + f[6] + f[7]);
    if (cs != f[8]) {
        return false;
    }

    pm25 = static_cast<float>(
               (static_cast<std::uint16_t>(f[3]) << 8U) | f[2]) /
           10.0F;
    pm10 = static_cast<float>(
               (static_cast<std::uint16_t>(f[5]) << 8U) | f[4]) /
           10.0F;
    return true;
}

}  // namespace

SensorType Sds011Sensor::type() const {
    return SensorType::kSds011;
}

esp_err_t Sds011Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    if (uart_port_manager_ == nullptr) {
        setError("UART port manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = uart_port_manager_->open(
        record_.uart_port_id,
        record_.uart_rx_gpio_pin,
        record_.uart_tx_gpio_pin,
        record_.uart_baud_rate);
    if (err != ESP_OK) {
        setError(
            std::string("Failed to open UART for SDS011: ") +
            esp_err_to_name(err));
        return err;
    }

    // Wake the sensor, then put it into continuous active-reporting mode.
    // Sensor.Community re-sends start commands during runtime; we do the same
    // recovery sequence whenever SDS011 stays silent, so the first init may
    // legitimately happen before the module is ready to accept commands.
    const bool configured = configureActiveMode(uart_port_manager_, record_.uart_port_id);
    if (!configured) {
        ESP_LOGW(kTag, "Initial SDS011 command sequence was not fully written");
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Sds011Sensor::poll() {
    if (!initialized_ || uart_port_manager_ == nullptr) {
        setError("SDS011 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t buf[kReadBufLen]{};
    const int bytes_read = uart_port_manager_->read(
        record_.uart_port_id,
        buf,
        sizeof(buf),
        pdMS_TO_TICKS(100U));

    if (bytes_read < 0) {
        setError("Failed to read SDS011 UART data.");
        initialized_ = false;
        return ESP_FAIL;
    }

    // Temporary diagnostic logging — remove once frame parsing is confirmed.
    {
        char hex[kReadBufLen * 3 + 1]{};
        int pos = 0;
        for (int i = 0; i < bytes_read && pos + 3 <= static_cast<int>(sizeof(hex)); ++i) {
            pos += std::snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);
        }
        ESP_LOGI(kTag, "read %d bytes: %s", bytes_read, hex);
    }

    if (bytes_read < static_cast<int>(kFrameLen)) {
        configureActiveMode(uart_port_manager_, record_.uart_port_id);
        setError("No SDS011 data received.");
        return ESP_OK;
    }

    // Scan for the last valid frame in the buffer and keep it.
    bool got_frame = false;
    float pm25 = 0.0F;
    float pm10 = 0.0F;

    const int scan_limit = bytes_read - static_cast<int>(kFrameLen);
    for (int i = 0; i <= scan_limit; ++i) {
        if (buf[i] != kFrameHead) {
            continue;
        }
        float candidate_pm25 = 0.0F;
        float candidate_pm10 = 0.0F;
        if (decodeFrame(&buf[i], candidate_pm25, candidate_pm10)) {
            pm25 = candidate_pm25;
            pm10 = candidate_pm10;
            got_frame = true;
            i += static_cast<int>(kFrameLen) - 1;
        } else {
            ESP_LOGI(kTag,
                "rejected at [%d]: %02X %02X .. tail=%02X cs_got=%02X cs_exp=%02X",
                i, buf[i],
                (i + 1 < bytes_read) ? buf[i + 1] : 0U,
                (i + 9 < bytes_read) ? buf[i + 9] : 0U,
                (i + 8 < bytes_read) ? buf[i + 8] : 0U,
                static_cast<std::uint8_t>(
                    ((i + 2 < bytes_read) ? buf[i + 2] : 0) +
                    ((i + 3 < bytes_read) ? buf[i + 3] : 0) +
                    ((i + 4 < bytes_read) ? buf[i + 4] : 0) +
                    ((i + 5 < bytes_read) ? buf[i + 5] : 0) +
                    ((i + 6 < bytes_read) ? buf[i + 6] : 0) +
                    ((i + 7 < bytes_read) ? buf[i + 7] : 0)));
        }
    }

    if (!got_frame) {
        configureActiveMode(uart_port_manager_, record_.uart_port_id);
        setError("No valid SDS011 frame found in received data.");
        return ESP_OK;
    }

    measurement_.clear();
    measurement_.sample_time_ms =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000LL);
    measurement_.addValue(SensorValueKind::kPm2_5UgM3, pm25);
    measurement_.addValue(SensorValueKind::kPm10_0UgM3, pm10);
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Sds011Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Sds011Sensor::lastError() const {
    return last_error_;
}

void Sds011Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createSds011Sensor() {
    return std::make_unique<Sds011Sensor>();
}

}  // namespace air360
