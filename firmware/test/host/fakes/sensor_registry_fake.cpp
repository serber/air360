// Host replacement for main/src/sensors/sensor_registry.cpp. The real
// registry links every driver and its ESP-IDF component; this one exposes two
// descriptors backed by a scriptable FakeSensorDriver (see
// fake_sensor_registry.hpp).
#include "air360/sensors/sensor_registry.hpp"

#include <chrono>
#include <thread>

#include "fake_sensor_registry.hpp"

namespace air360 {
namespace {

class FakeSensorDriver final : public SensorDriver {
  public:
    explicit FakeSensorDriver(SensorType type) : type_(type) {}
    ~FakeSensorDriver() override {
        if (behaviour_ != nullptr) {
            ++behaviour_->drivers_destroyed;
        }
    }

    SensorType type() const override { return type_; }

    esp_err_t init(const SensorRecord& record, const SensorDriverContext&) override {
        if (behaviour_ == nullptr) {
            behaviour_ = &fake_sensors::behaviour(record.id);
            ++behaviour_->drivers_created;
        }
        ++behaviour_->init_calls;
        const esp_err_t result = behaviour_->init_result.load();
        initialized_ = result == ESP_OK;
        return result;
    }

    esp_err_t poll() override {
        ++behaviour_->poll_calls;
        const int delay_ms = behaviour_->poll_delay_ms.load();
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        const esp_err_t result = behaviour_->poll_result.load();
        if (result != ESP_OK) {
            return reportPollFailure("air360.fake", "fake poll failure", result);
        }
        if (behaviour_->fresh_samples.load() || measurement_.empty()) {
            ++sample_counter_;
            measurement_.clear();
            measurement_.sample_time_ms = sample_counter_;
            measurement_.addValue(SensorValueKind::kTemperatureC,
                                  static_cast<float>(sample_counter_));
        }
        notePollSuccess();
        return ESP_OK;
    }

    SensorMeasurement latestMeasurement() const override { return measurement_; }

  private:
    SensorType type_;
    fake_sensors::Behaviour* behaviour_ = nullptr;
    SensorMeasurement measurement_{};
    std::uint64_t sample_counter_ = 0U;
};

std::unique_ptr<SensorDriver> createFakeSht4x() {
    return std::make_unique<FakeSensorDriver>(SensorType::kSht4x);
}

std::unique_ptr<SensorDriver> createFakeIna219() {
    return std::make_unique<FakeSensorDriver>(SensorType::kIna219);
}

SensorDescriptor makeI2cDescriptor(SensorType type, const char* key, SensorDriverFactory factory,
                                   SensorStartupPhase phase) {
    SensorDescriptor descriptor{};
    descriptor.type = type;
    descriptor.type_key = key;
    descriptor.display_name = key;
    descriptor.supports_i2c = true;
    descriptor.driver_implemented = true;
    descriptor.default_poll_interval_ms = 60000U;
    descriptor.create_driver = factory;
    descriptor.startup_phase = phase;
    return descriptor;
}

const std::array<SensorDescriptor, 2U>& descriptorTable() {
    static const std::array<SensorDescriptor, 2U> table{{
        makeI2cDescriptor(SensorType::kSht4x, "sht4x", &createFakeSht4x,
                          SensorStartupPhase::kAfterNetwork),
        makeI2cDescriptor(SensorType::kIna219, "ina219", &createFakeIna219,
                          SensorStartupPhase::kBeforeNetwork),
    }};
    return table;
}

}  // namespace

const SensorDescriptor* SensorRegistry::descriptors() const {
    return descriptorTable().data();
}

std::size_t SensorRegistry::descriptorCount() const {
    return descriptorTable().size();
}

const SensorDescriptor* SensorRegistry::findByType(SensorType type) const {
    for (const auto& descriptor : descriptorTable()) {
        if (descriptor.type == type) {
            return &descriptor;
        }
    }
    return nullptr;
}

const SensorDescriptor* SensorRegistry::findByTypeKey(const std::string& type_key) const {
    for (const auto& descriptor : descriptorTable()) {
        if (type_key == descriptor.type_key) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool SensorRegistry::supportsTransport(const SensorDescriptor& descriptor, TransportKind kind) const {
    return kind == TransportKind::kI2c && descriptor.supports_i2c;
}

bool SensorRegistry::validateRecord(const SensorRecord& record, std::string& error) const {
    const SensorDescriptor* descriptor = findByType(record.sensor_type);
    if (descriptor == nullptr) {
        error = "Unsupported sensor type.";
        return false;
    }
    if (!supportsTransport(*descriptor, record.transport_kind)) {
        error = "Unsupported transport.";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace air360
