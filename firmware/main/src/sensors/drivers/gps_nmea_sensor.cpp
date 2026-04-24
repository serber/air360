#include "air360/sensors/drivers/gps_nmea_sensor.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.gps";
// UART framing cost for 8N1: 1 start + 8 data + 1 stop bit. This lets the
// driver derive a byte budget from configured baud and poll cadence.
constexpr std::uint32_t kGpsSerialBitsPerByte = 10U;
// 256-byte chunks keep stack usage low while still amortizing UART API calls.
constexpr std::size_t kGpsReadBufferSize = 256U;
// Allow the first read to block for 80% of the poll cadence so low-baud GPS
// modules can accumulate a whole NMEA burst before the drain-to-empty loop.
constexpr std::uint32_t kGpsReadTimeoutPercent = 80U;
// Never poll with a sub-50 ms initial timeout; shorter waits were observed to
// starve slow receivers even when the configured poll interval is small.
constexpr std::uint32_t kGpsReadTimeoutMinMs = 50U;
// Keep one extra chunk above the derived baud×interval budget so sentence tails
// that arrive near the poll boundary still fit in the same cycle.
constexpr std::size_t kGpsPollBudgetMarginBytes = kGpsReadBufferSize;
// 4 KB is the floor because it is cheap on ESP32-S3 and large enough to absorb
// multi-second NMEA bursts at common baud rates between worker polls.
constexpr std::size_t kGpsMinimumRxBufferSize = 4096U;
// A short event queue is enough because the driver drains it every poll and
// only cares about overrun/full events, not every UART edge transition.
constexpr std::size_t kGpsEventQueueSize = 8U;

}  // namespace

GpsNmeaSensor::GpsNmeaSensor() = default;

GpsNmeaSensor::~GpsNmeaSensor() = default;

SensorType GpsNmeaSensor::type() const {
    return SensorType::kGpsNmea;
}

void GpsNmeaSensor::resetParser() {
    // Placement-new avoids heap churn on re-init: the parser lives in-place
    // as a data member, so destroy-then-reconstruct keeps allocations out of
    // the sensor poll/init hot path.
    parser_.~TinyGPSPlus();
    new (&parser_) TinyGPSPlus();
}

esp_err_t GpsNmeaSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    record_ = record;
    uart_port_manager_ = context.uart_port_manager;
    resetParser();
    measurement_.clear();
    last_error_.clear();
    soft_fail_policy_.onPollOk();
    max_bytes_per_poll_ = computeMaxBytesPerPoll();
    read_timeout_ticks_ = computeReadTimeoutTicks();
    uart_overrun_count_ = 0U;
    initialized_ = false;

    if (uart_port_manager_ == nullptr) {
        setError("UART port manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    const std::size_t rx_buffer_size = std::max(
        kGpsMinimumRxBufferSize,
        max_bytes_per_poll_ + kGpsReadBufferSize);
    const esp_err_t err = uart_port_manager_->open(
        record_.uart_port_id,
        record_.uart_rx_gpio_pin,
        record_.uart_tx_gpio_pin,
        record_.uart_baud_rate,
        rx_buffer_size,
        kGpsEventQueueSize);
    if (err != ESP_OK) {
        setError("Failed to open configured UART for GPS sensor.");
        return err;
    }

    uart_port_manager_->flush(record_.uart_port_id);
    initialized_ = true;
    return ESP_OK;
}

esp_err_t GpsNmeaSensor::poll() {
    if (!initialized_ || uart_port_manager_ == nullptr) {
        setError("GPS sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = drainUartEvents();
    if (err != ESP_OK) {
        setError("Failed to process GPS UART events.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    std::uint8_t buffer[kGpsReadBufferSize]{};
    std::size_t total_bytes_read = 0U;
    int bytes_read = 0;
    while (true) {
        std::size_t buffered_length = 0U;
        err = uart_port_manager_->bufferedDataLength(record_.uart_port_id, buffered_length);
        if (err != ESP_OK) {
            setError("Failed to inspect GPS UART buffer.");
            if (soft_fail_policy_.onPollErr()) {
                ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
                initialized_ = false;
            } else if (soft_fail_policy_.soft_fails == 1U) {
                ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            }
            return err;
        }

        const TickType_t timeout_ticks =
            total_bytes_read == 0U ? read_timeout_ticks_ : 0;
        const std::size_t read_size = total_bytes_read == 0U
            ? sizeof(buffer)
            : std::min(sizeof(buffer), buffered_length);
        if (read_size == 0U) {
            break;
        }

        bytes_read = uart_port_manager_->read(
            record_.uart_port_id,
            buffer,
            read_size,
            timeout_ticks);
        if (bytes_read < 0) {
            setError("Failed to read GPS UART data.");
            if (soft_fail_policy_.onPollErr()) {
                ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
                initialized_ = false;
            } else if (soft_fail_policy_.soft_fails == 1U) {
                ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            }
            return ESP_FAIL;
        }

        if (bytes_read == 0) {
            break;
        }

        total_bytes_read += static_cast<std::size_t>(bytes_read);
        for (int index = 0; index < bytes_read; ++index) {
            parser_.encode(static_cast<char>(buffer[index]));
        }
    }

    err = drainUartEvents();
    if (err != ESP_OK) {
        setError("Failed to process GPS UART events.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    if (total_bytes_read == 0U) {
        soft_fail_policy_.onPollOk();
        setError("No GPS UART data received.");
        return ESP_OK;
    }

    rebuildMeasurement();
    soft_fail_policy_.onPollOk();
    return ESP_OK;
}

SensorMeasurement GpsNmeaSensor::latestMeasurement() const {
    return measurement_;
}

std::string GpsNmeaSensor::lastError() const {
    return last_error_;
}

esp_err_t GpsNmeaSensor::drainUartEvents() {
    if (uart_port_manager_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    UartPortManager::EventSummary event_summary{};
    const esp_err_t err = uart_port_manager_->drainEvents(
        record_.uart_port_id,
        event_summary);
    if (err != ESP_OK) {
        return err;
    }

    if (event_summary.overrun_count > 0U) {
        uart_overrun_count_ += event_summary.overrun_count;
        ESP_LOGW(
            kTag,
            "GPS UART overrun events=%u total=%u port=%u baud=%" PRIu32 " budget=%u timeout_ms=%" PRIu32,
            static_cast<unsigned>(event_summary.overrun_count),
            static_cast<unsigned>(uart_overrun_count_),
            static_cast<unsigned>(record_.uart_port_id),
            record_.uart_baud_rate,
            static_cast<unsigned>(max_bytes_per_poll_),
            static_cast<std::uint32_t>(record_.poll_interval_ms * kGpsReadTimeoutPercent / 100U));
    }

    return ESP_OK;
}

std::size_t GpsNmeaSensor::computeMaxBytesPerPoll() const {
    if (record_.uart_baud_rate == 0U || record_.poll_interval_ms == 0U) {
        return kGpsReadBufferSize;
    }

    const std::uint64_t bytes_per_second =
        (static_cast<std::uint64_t>(record_.uart_baud_rate) + (kGpsSerialBitsPerByte - 1U)) /
        kGpsSerialBitsPerByte;
    const std::uint64_t bytes_for_interval =
        (bytes_per_second * static_cast<std::uint64_t>(record_.poll_interval_ms) + 999ULL) / 1000ULL;
    const std::uint64_t budget = bytes_for_interval + static_cast<std::uint64_t>(kGpsPollBudgetMarginBytes);
    return static_cast<std::size_t>(std::max<std::uint64_t>(budget, kGpsReadBufferSize));
}

TickType_t GpsNmeaSensor::computeReadTimeoutTicks() const {
    const std::uint32_t timeout_ms = std::max(
        kGpsReadTimeoutMinMs,
        (record_.poll_interval_ms * kGpsReadTimeoutPercent) / 100U);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    return timeout_ticks > 0 ? timeout_ticks : 1;
}

void GpsNmeaSensor::rebuildMeasurement() {
    measurement_.clear();

    bool has_values = false;
    const bool location_valid = parser_.location.isValid();
    if (location_valid) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kLatitudeDeg,
            static_cast<float>(parser_.location.lat()));
        measurement_.addValue(
            SensorValueKind::kLongitudeDeg,
            static_cast<float>(parser_.location.lng()));
    }
    if (parser_.altitude.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kAltitudeM,
            static_cast<float>(parser_.altitude.meters()));
    }
    if (parser_.satellites.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kSatellites,
            static_cast<float>(parser_.satellites.value()));
    }
    if (parser_.speed.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kSpeedKnots,
            static_cast<float>(parser_.speed.knots()));
    }
    if (parser_.course.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kCourseDeg,
            static_cast<float>(parser_.course.deg()));
    }
    if (parser_.hdop.isValid()) {
        if (!has_values) {
            measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
            has_values = true;
        }
        measurement_.addValue(
            SensorValueKind::kHdop,
            static_cast<float>(parser_.hdop.hdop()));
    }

    if (location_valid) {
        last_error_.clear();
    } else {
        setError("No GPS fix yet.");
    }
}

void GpsNmeaSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createGpsNmeaSensor() {
    return std::make_unique<GpsNmeaSensor>();
}

}  // namespace air360
