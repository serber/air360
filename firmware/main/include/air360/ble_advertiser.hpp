#pragma once

#include <cstdint>

#include "air360/config_repository.hpp"
#include "air360/uploads/measurement_store.hpp"

namespace air360 {

struct BleState {
    bool enabled = false;
    bool running = false;
    std::uint16_t adv_interval_ms = 0U;
};

class BleAdvertiser {
  public:
    BleAdvertiser() = default;
    BleAdvertiser(const BleAdvertiser&) = delete;
    BleAdvertiser& operator=(const BleAdvertiser&) = delete;
    BleAdvertiser(BleAdvertiser&&) = delete;
    BleAdvertiser& operator=(BleAdvertiser&&) = delete;

    void start(const DeviceConfig& config, MeasurementStore& store);
    void stop();
    BleState state() const;

  private:
    static void taskEntry(void* arg);
    void taskMain();
    void updateAdvertisement();
    std::uint8_t buildPayload(std::uint8_t* buf, std::uint8_t max_len);

    MeasurementStore* store_ = nullptr;
    std::uint16_t adv_interval_ms_ = 1000U;
    bool enabled_ = false;
    bool running_ = false;
    char device_name_[32] = {};
    void* task_handle_ = nullptr;
};

}  // namespace air360
