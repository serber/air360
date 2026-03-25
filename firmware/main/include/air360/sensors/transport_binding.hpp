#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace air360 {

class I2cBusManager {
  public:
    ~I2cBusManager();

    esp_err_t probe(std::uint8_t bus_id, std::uint8_t address);
    esp_err_t writeRegister(
        std::uint8_t bus_id,
        std::uint8_t address,
        std::uint8_t reg,
        std::uint8_t value);
    esp_err_t readRegister(
        std::uint8_t bus_id,
        std::uint8_t address,
        std::uint8_t reg,
        std::uint8_t* buffer,
        std::size_t buffer_size);
    void shutdown();

  private:
    struct BusState {
        bool initialized = false;
        i2c_master_bus_handle_t handle = nullptr;
    };

    esp_err_t ensureBus(std::uint8_t bus_id, BusState*& out_state);
    esp_err_t withDevice(
        std::uint8_t bus_id,
        std::uint8_t address,
        i2c_master_dev_handle_t& out_device);
    void releaseDevice(i2c_master_dev_handle_t device);

    std::array<BusState, 2> buses_{};
};

}  // namespace air360
