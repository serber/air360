#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_bus.h"

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
    esp_err_t write(
        std::uint8_t bus_id,
        std::uint8_t address,
        std::uint8_t reg,
        const std::uint8_t* buffer,
        std::size_t buffer_size);
    esp_err_t writeRaw(
        std::uint8_t bus_id,
        std::uint8_t address,
        const std::uint8_t* buffer,
        std::size_t buffer_size);
    esp_err_t readRaw(
        std::uint8_t bus_id,
        std::uint8_t address,
        std::uint8_t* buffer,
        std::size_t buffer_size);
    esp_err_t readRegister(
        std::uint8_t bus_id,
        std::uint8_t address,
        std::uint8_t reg,
        std::uint8_t* buffer,
        std::size_t buffer_size);
    esp_err_t getComponentBus(
        std::uint8_t bus_id,
        i2c_bus_handle_t& out_handle);
    void shutdown();

  private:
    struct DeviceState {
        bool initialized = false;
        std::uint8_t address = 0U;
        i2c_master_dev_handle_t handle = nullptr;
    };

    struct BusState {
        bool initialized = false;
        i2c_master_bus_handle_t handle = nullptr;
        std::array<DeviceState, 8> devices{};
    };

    void ensureMutex();
    void lock();
    void unlock();
    esp_err_t ensureBus(std::uint8_t bus_id, BusState*& out_state);
    esp_err_t ensureDevice(
        std::uint8_t bus_id,
        std::uint8_t address,
        BusState*& out_bus,
        i2c_master_dev_handle_t& out_device);
    void releaseDevices(BusState& bus);

    StaticSemaphore_t mutex_buffer_{};
    SemaphoreHandle_t mutex_ = nullptr;
    std::array<BusState, 2> buses_{};
};

class UartPortManager {
  public:
    ~UartPortManager();

    esp_err_t open(
        std::uint8_t port_id,
        std::int16_t rx_pin,
        std::int16_t tx_pin,
        std::uint32_t baud_rate);
    int read(
        std::uint8_t port_id,
        std::uint8_t* buffer,
        std::size_t buffer_size,
        TickType_t timeout_ticks);
    esp_err_t flush(std::uint8_t port_id);
    void shutdown();

  private:
    struct PortState {
        bool initialized = false;
        int port_number = 0;
        std::int16_t rx_pin = -1;
        std::int16_t tx_pin = -1;
        std::uint32_t baud_rate = 0U;
    };

    esp_err_t ensurePort(
        std::uint8_t port_id,
        std::int16_t rx_pin,
        std::int16_t tx_pin,
        std::uint32_t baud_rate,
        PortState*& out_state);
    static bool resolvePort(std::uint8_t port_id, int& out_port_number);

    std::array<PortState, 2> ports_{};
};

}  // namespace air360
