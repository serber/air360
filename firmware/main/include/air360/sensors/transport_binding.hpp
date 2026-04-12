#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "i2cdev.h"

#include "air360/sensors/sensor_config.hpp"

namespace air360 {

// Owns the i2cdev subsystem lifecycle for all I2C sensor drivers.
// Call init() once before any driver initialises. Drivers use resolvePins()
// and setupDevice() instead of calling i2cdev_init() or hardcoding GPIO numbers.
class I2cBusManager {
  public:
    // Initialise the i2cdev subsystem. Idempotent — safe to call on every
    // applyConfig(). Must be called before any driver's init().
    esp_err_t init();

    // Fill out_port / out_sda / out_scl for the given logical bus id.
    // Returns false if the bus id is not known.
    bool resolvePins(
        std::uint8_t bus_id,
        i2c_port_t& out_port,
        gpio_num_t& out_sda,
        gpio_num_t& out_scl) const;

    // Populate out_dev from the sensor record and call i2c_dev_create_mutex().
    // Used by drivers that manage their own i2c_dev_t directly (SPS30).
    esp_err_t setupDevice(
        const SensorRecord& record,
        std::uint32_t speed_hz,
        i2c_dev_t& out_dev) const;

    // Return an i2c_bus_handle_t that shares the bus already initialised by
    // i2cdev. Used by components built on espressif__i2c_bus (BME280).
    esp_err_t getComponentBus(
        std::uint8_t bus_id,
        i2c_bus_handle_t& out_handle) const;
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
