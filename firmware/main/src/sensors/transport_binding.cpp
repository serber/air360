#include "air360/sensors/transport_binding.hpp"

#include <cstddef>
#include <cstdint>

namespace air360 {

namespace {

constexpr int kI2cTransferTimeoutMs = 200;
constexpr std::uint32_t kI2cClockHz = 100000U;
constexpr gpio_num_t kBus0Sda = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SDA_GPIO);
constexpr gpio_num_t kBus0Scl = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SCL_GPIO);

bool resolveBusPins(
    std::uint8_t bus_id,
    i2c_port_num_t& port,
    gpio_num_t& sda_pin,
    gpio_num_t& scl_pin) {
    if (bus_id == 0U) {
        port = I2C_NUM_0;
        sda_pin = kBus0Sda;
        scl_pin = kBus0Scl;
        return true;
    }

    return false;
}

}  // namespace

I2cBusManager::~I2cBusManager() {
    shutdown();
}

esp_err_t I2cBusManager::ensureBus(std::uint8_t bus_id, BusState*& out_state) {
    if (bus_id >= buses_.size()) {
        return ESP_ERR_INVALID_ARG;
    }

    out_state = &buses_[bus_id];
    if (out_state->initialized) {
        return ESP_OK;
    }

    i2c_port_num_t port = I2C_NUM_0;
    gpio_num_t sda_pin = GPIO_NUM_NC;
    gpio_num_t scl_pin = GPIO_NUM_NC;
    if (!resolveBusPins(bus_id, port, sda_pin, scl_pin)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = port;
    bus_config.sda_io_num = sda_pin;
    bus_config.scl_io_num = scl_pin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.intr_priority = 0;
    bus_config.trans_queue_depth = 4;
    bus_config.flags.enable_internal_pullup = 1;
    bus_config.flags.allow_pd = 0;

    esp_err_t err = i2c_new_master_bus(&bus_config, &out_state->handle);
    if (err != ESP_OK) {
        return err;
    }

    out_state->initialized = true;
    return ESP_OK;
}

esp_err_t I2cBusManager::withDevice(
    std::uint8_t bus_id,
    std::uint8_t address,
    i2c_master_dev_handle_t& out_device) {
    out_device = nullptr;

    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = kI2cClockHz;
    device_config.scl_wait_us = 0;
    device_config.flags.disable_ack_check = 0;

    return i2c_master_bus_add_device(bus_state->handle, &device_config, &out_device);
}

void I2cBusManager::releaseDevice(i2c_master_dev_handle_t device) {
    if (device != nullptr) {
        i2c_master_bus_rm_device(device);
    }
}

esp_err_t I2cBusManager::probe(std::uint8_t bus_id, std::uint8_t address) {
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        return err;
    }

    return i2c_master_probe(bus_state->handle, address, kI2cTransferTimeoutMs);
}

esp_err_t I2cBusManager::writeRegister(
    std::uint8_t bus_id,
    std::uint8_t address,
    std::uint8_t reg,
    std::uint8_t value) {
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = withDevice(bus_id, address, device);
    if (err != ESP_OK) {
        return err;
    }

    const std::uint8_t buffer[2] = {reg, value};
    err = i2c_master_transmit(device, buffer, sizeof(buffer), kI2cTransferTimeoutMs);
    releaseDevice(device);
    return err;
}

esp_err_t I2cBusManager::readRegister(
    std::uint8_t bus_id,
    std::uint8_t address,
    std::uint8_t reg,
    std::uint8_t* buffer,
    std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = withDevice(bus_id, address, device);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit_receive(
        device,
        &reg,
        1U,
        buffer,
        buffer_size,
        kI2cTransferTimeoutMs);
    releaseDevice(device);
    return err;
}

void I2cBusManager::shutdown() {
    for (auto& bus : buses_) {
        if (!bus.initialized || bus.handle == nullptr) {
            continue;
        }

        i2c_del_master_bus(bus.handle);
        bus.handle = nullptr;
        bus.initialized = false;
    }
}

}  // namespace air360
