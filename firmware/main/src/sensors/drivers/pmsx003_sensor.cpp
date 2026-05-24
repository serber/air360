#include "air360/sensors/drivers/pmsx003_sensor.hpp"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pms.h"

namespace air360 {

namespace {
constexpr char kTag[] = "air360.sensor.pmsx003";
constexpr std::size_t kPmsx003FrameSize = 32U;
constexpr std::size_t kPmsx003ReadBufferSize = 128U;
constexpr std::size_t kPmsx003EventQueueSize = 8U;
constexpr std::uint32_t kPmsx003ReadTimeoutMs = 100U;
constexpr std::uint8_t kPmsFrameHeadHigh = 0x42U;
constexpr std::uint8_t kPmsFrameHeadLow = 0x4DU;
constexpr std::uint8_t kPmsx003FrameLengthHigh = 0x00U;
constexpr std::uint8_t kPmsx003FrameLengthLow = 0x1CU;
}  // namespace

Pmsx003Sensor::~Pmsx003Sensor() {
    const esp_err_t err = pms_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to deinit PMSX003 component: %s", esp_err_to_name(err));
    }
}

SensorType Pmsx003Sensor::type() const {
    return SensorType::kPmsx003;
}

esp_err_t Pmsx003Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();
    resetParser();
    uart_overrun_count_ = 0U;
    initialized_ = false;

    if (uart_port_manager_ == nullptr) {
        setError("UART port manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t open_err = uart_port_manager_->open(
        record_.uart_port_id,
        record_.uart_rx_gpio_pin,
        record_.uart_tx_gpio_pin,
        record_.uart_baud_rate,
        UartPortManager::kDefaultRxBufferSize,
        kPmsx003EventQueueSize);
    if (open_err != ESP_OK) {
        setError("Failed to open configured UART for PMSX003 sensor.");
        return open_err;
    }

    const esp_err_t deinit_err = pms_deinit();
    if (deinit_err != ESP_OK) {
        setError("Failed to reset PMSX003 component state.");
        return deinit_err;
    }

    pms_config_t config{};
    config.type = PMS_TYPE_5003;
    config.set_gpio = GPIO_NUM_NC;
    config.reset_gpio = GPIO_NUM_NC;
    config.uart_port = static_cast<uart_port_t>(record_.uart_port_id);

    const esp_err_t init_err = pms_init(&config);
    if (init_err != ESP_OK) {
        setError(std::string("PMSX003 component init failed: ") + esp_err_to_name(init_err) + ".");
        return init_err;
    }

    const esp_err_t flush_err = uart_port_manager_->flush(record_.uart_port_id);
    if (flush_err != ESP_OK) {
        setError("Failed to flush PMSX003 UART input.");
        return flush_err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Pmsx003Sensor::poll() {
    if (!initialized_ || uart_port_manager_ == nullptr) {
        setError("PMSX003 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = drainUartEvents();
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to process PMSX003 UART events.");
    }

    const pms_state_e state = pms_get_state();
    if (state != PMS_STATE_ACTIVE) {
        setError("PMSX003 is warming up.");
        soft_fail_policy_.onPollOk();
        return ESP_OK;
    }

    bool found_frame = false;
    err = readAvailableFrames(found_frame);
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to read PMSX003 UART data.");
    }

    err = drainUartEvents();
    if (err != ESP_OK) {
        return handlePollError(err, "Failed to process PMSX003 UART events.");
    }

    soft_fail_policy_.onPollOk();
    if (found_frame) {
        clearError();
    }
    return ESP_OK;
}

SensorMeasurement Pmsx003Sensor::latestMeasurement() const {
    return measurement_;
}

esp_err_t Pmsx003Sensor::drainUartEvents() {
    if (uart_port_manager_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    UartPortManager::EventSummary event_summary{};
    const esp_err_t err = uart_port_manager_->drainEvents(record_.uart_port_id, event_summary);
    if (err != ESP_OK) {
        return err;
    }

    if (event_summary.overrun_count > 0U) {
        uart_overrun_count_ += event_summary.overrun_count;
        ESP_LOGW(
            kTag,
            "PMSX003 UART overrun events=%u total=%" PRIu32 " port=%u",
            static_cast<unsigned>(event_summary.overrun_count),
            uart_overrun_count_,
            static_cast<unsigned>(record_.uart_port_id));
        resetParser();
    }

    return ESP_OK;
}

esp_err_t Pmsx003Sensor::readAvailableFrames(bool& out_found_frame) {
    out_found_frame = false;
    std::uint8_t buffer[kPmsx003ReadBufferSize]{};
    std::size_t total_bytes_read = 0U;

    while (true) {
        const TickType_t timeout_ticks =
            total_bytes_read == 0U ? pdMS_TO_TICKS(kPmsx003ReadTimeoutMs) : 0;
        const int bytes_read = uart_port_manager_->read(
            record_.uart_port_id,
            buffer,
            sizeof(buffer),
            timeout_ticks);
        if (bytes_read < 0) {
            return ESP_FAIL;
        }

        if (bytes_read == 0) {
            break;
        }

        total_bytes_read += static_cast<std::size_t>(bytes_read);
        for (int index = 0; index < bytes_read; ++index) {
            bool frame_ready = false;
            feedByte(buffer[index], frame_ready);
            if (frame_ready) {
                const esp_err_t decode_err = decodeFrame();
                if (decode_err == ESP_OK) {
                    storeMeasurement();
                    out_found_frame = true;
                }
            }
        }

        std::size_t buffered_length = 0U;
        const esp_err_t buffered_err =
            uart_port_manager_->bufferedDataLength(record_.uart_port_id, buffered_length);
        if (buffered_err != ESP_OK) {
            return buffered_err;
        }
        if (buffered_length == 0U) {
            break;
        }
    }

    if (!out_found_frame) {
        setError(
            total_bytes_read == 0U
                ? "No PMSX003 UART data received."
                : "No valid PMSX003 frame received.");
    }

    return ESP_OK;
}

void Pmsx003Sensor::feedByte(std::uint8_t byte, bool& out_frame_ready) {
    out_frame_ready = false;

    if (frame_index_ == 0U) {
        if (byte == kPmsFrameHeadHigh) {
            frame_[frame_index_++] = byte;
        }
        return;
    }

    if (frame_index_ == 1U && byte != kPmsFrameHeadLow) {
        frame_index_ = byte == kPmsFrameHeadHigh ? 1U : 0U;
        frame_[0] = byte == kPmsFrameHeadHigh ? byte : 0U;
        return;
    }

    if (frame_index_ == 2U && byte != kPmsx003FrameLengthHigh) {
        resetParser();
        return;
    }

    if (frame_index_ == 3U && byte != kPmsx003FrameLengthLow) {
        resetParser();
        return;
    }

    frame_[frame_index_++] = byte;
    if (frame_index_ < kPmsx003FrameSize) {
        return;
    }

    out_frame_ready = true;
    frame_index_ = 0U;
}

esp_err_t Pmsx003Sensor::decodeFrame() {
    return pms_parse_data(frame_.data(), frame_.size());
}

void Pmsx003Sensor::storeMeasurement() {
    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(
        SensorValueKind::kPm1_0UgM3,
        static_cast<float>(pms_get_data(PMS_FIELD_PM1_ATM)));
    measurement_.addValue(
        SensorValueKind::kPm2_5UgM3,
        static_cast<float>(pms_get_data(PMS_FIELD_PM2_5_ATM)));
    measurement_.addValue(
        SensorValueKind::kPm10_0UgM3,
        static_cast<float>(pms_get_data(PMS_FIELD_PM10_ATM)));
    measurement_.addValue(
        SensorValueKind::kPc0_3Per0_1L,
        static_cast<float>(pms_get_data(PMS_FIELD_PC_0_3)));
    measurement_.addValue(
        SensorValueKind::kPc0_5Per0_1L,
        static_cast<float>(pms_get_data(PMS_FIELD_PC_0_5)));
    measurement_.addValue(
        SensorValueKind::kPc1_0Per0_1L,
        static_cast<float>(pms_get_data(PMS_FIELD_PC_1_0)));
    measurement_.addValue(
        SensorValueKind::kPc2_5Per0_1L,
        static_cast<float>(pms_get_data(PMS_FIELD_PC_2_5)));
    measurement_.addValue(
        SensorValueKind::kPc5_0Per0_1L,
        static_cast<float>(pms_get_data(PMS_FIELD_PC_5_0)));
    measurement_.addValue(
        SensorValueKind::kPc10Per0_1L,
        static_cast<float>(pms_get_data(PMS_FIELD_PC_10)));
}

esp_err_t Pmsx003Sensor::handlePollError(esp_err_t err, const char* message) {
    const esp_err_t result = reportPollFailure(
        kTag,
        message != nullptr ? std::string(message) : std::string(esp_err_to_name(err)),
        err);
    if (!initialized_) {
        resetParser();
    }
    return result;
}

void Pmsx003Sensor::resetParser() {
    frame_.fill(0U);
    frame_index_ = 0U;
}

std::unique_ptr<SensorDriver> createPmsx003Sensor() {
    return std::make_unique<Pmsx003Sensor>();
}

}  // namespace air360
