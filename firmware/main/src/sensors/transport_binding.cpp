#include "air360/sensors/transport_binding.hpp"

#include <cstddef>
#include <cstdint>

#include "driver/uart.h"

namespace air360 {

namespace {

constexpr int kI2cTransferTimeoutMs = 200;
constexpr std::uint32_t kI2cClockHz = 100000U;
constexpr TickType_t kI2cTransferTimeoutTicks = pdMS_TO_TICKS(kI2cTransferTimeoutMs);
constexpr int kUartRxBufferSize = 1024;
constexpr int kUartTxBufferSize = 0;
constexpr gpio_num_t kBus0Sda = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SDA_GPIO);
constexpr gpio_num_t kBus0Scl = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SCL_GPIO);

bool resolveBusPins(
    std::uint8_t bus_id,
    i2c_port_t& port,
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

void I2cBusManager::ensureMutex() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    }
}

void I2cBusManager::lock() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void I2cBusManager::unlock() {
    xSemaphoreGive(mutex_);
}

esp_err_t I2cBusManager::ensureBus(std::uint8_t bus_id, BusState*& out_state) {
    if (bus_id >= buses_.size()) {
        return ESP_ERR_INVALID_ARG;
    }

    out_state = &buses_[bus_id];
    if (out_state->initialized) {
        return ESP_OK;
    }

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda_pin = GPIO_NUM_NC;
    gpio_num_t scl_pin = GPIO_NUM_NC;
    if (!resolveBusPins(bus_id, port, sda_pin, scl_pin)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    i2c_config_t config{};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = sda_pin;
    config.scl_io_num = scl_pin;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = kI2cClockHz;
    config.clk_flags = 0;

    esp_err_t err = i2c_param_config(port, &config);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        return err;
    }

    out_state->port = port;
    out_state->initialized = true;
    return ESP_OK;
}

esp_err_t I2cBusManager::probe(std::uint8_t bus_id, std::uint8_t address) {
    ensureMutex();
    lock();
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(
        cmd,
        static_cast<std::uint8_t>((address << 1U) | I2C_MASTER_WRITE),
        true);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(bus_state->port, cmd, kI2cTransferTimeoutTicks);
    i2c_cmd_link_delete(cmd);
    unlock();
    return err;
}

esp_err_t I2cBusManager::writeToAddress(
    BusState& bus,
    std::uint8_t address,
    const std::uint8_t* buffer,
    std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_to_device(
        bus.port,
        address,
        buffer,
        buffer_size,
        kI2cTransferTimeoutTicks);
}

esp_err_t I2cBusManager::writeReadToAddress(
    BusState& bus,
    std::uint8_t address,
    const std::uint8_t* tx_buffer,
    std::size_t tx_size,
    std::uint8_t* rx_buffer,
    std::size_t rx_size) {
    if (tx_buffer == nullptr || tx_size == 0U || rx_buffer == nullptr || rx_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(
        bus.port,
        address,
        tx_buffer,
        tx_size,
        rx_buffer,
        rx_size,
        kI2cTransferTimeoutTicks);
}

esp_err_t I2cBusManager::writeRegister(
    std::uint8_t bus_id,
    std::uint8_t address,
    std::uint8_t reg,
    std::uint8_t value) {
    return write(bus_id, address, reg, &value, 1U);
}

esp_err_t I2cBusManager::write(
    std::uint8_t bus_id,
    std::uint8_t address,
    std::uint8_t reg,
    const std::uint8_t* buffer,
    std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    ensureMutex();
    lock();
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    std::uint8_t staging[32];
    if (buffer_size + 1U > sizeof(staging)) {
        unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    staging[0] = reg;
    for (std::size_t index = 0; index < buffer_size; ++index) {
        staging[index + 1U] = buffer[index];
    }

    err = writeToAddress(*bus_state, address, staging, buffer_size + 1U);
    unlock();
    return err;
}

esp_err_t I2cBusManager::writeRaw(
    std::uint8_t bus_id,
    std::uint8_t address,
    const std::uint8_t* buffer,
    std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    ensureMutex();
    lock();
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = writeToAddress(*bus_state, address, buffer, buffer_size);
    unlock();
    return err;
}

esp_err_t I2cBusManager::readRaw(
    std::uint8_t bus_id,
    std::uint8_t address,
    std::uint8_t* buffer,
    std::size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    ensureMutex();
    lock();
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = i2c_master_read_from_device(
        bus_state->port,
        address,
        buffer,
        buffer_size,
        kI2cTransferTimeoutTicks);
    unlock();
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

    ensureMutex();
    lock();
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = writeReadToAddress(
        *bus_state,
        address,
        &reg,
        1U,
        buffer,
        buffer_size);
    unlock();
    return err;
}

void I2cBusManager::shutdown() {
    ensureMutex();
    lock();
    for (auto& bus : buses_) {
        if (!bus.initialized) {
            continue;
        }

        i2c_driver_delete(bus.port);
        bus.initialized = false;
    }
    unlock();
}

UartPortManager::~UartPortManager() {
    shutdown();
}

bool UartPortManager::resolvePort(std::uint8_t port_id, int& out_port_number) {
    switch (port_id) {
        case 1U:
            out_port_number = UART_NUM_1;
            return true;
        case 2U:
            out_port_number = UART_NUM_2;
            return true;
        default:
            return false;
    }
}

esp_err_t UartPortManager::ensurePort(
    std::uint8_t port_id,
    std::int16_t rx_pin,
    std::int16_t tx_pin,
    std::uint32_t baud_rate,
    PortState*& out_state) {
    if (port_id == 0U || port_id > ports_.size()) {
        return ESP_ERR_INVALID_ARG;
    }

    out_state = &ports_[port_id - 1U];
    if (out_state->initialized) {
        if (out_state->rx_pin == rx_pin &&
            out_state->tx_pin == tx_pin &&
            out_state->baud_rate == baud_rate) {
            return ESP_OK;
        }
        return ESP_ERR_INVALID_STATE;
    }

    int port_number = 0;
    if (!resolvePort(port_id, port_number)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uart_port_t port = static_cast<uart_port_t>(port_number);

    uart_config_t config{};
    config.baud_rate = static_cast<int>(baud_rate);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(port, kUartRxBufferSize, kUartTxBufferSize, 0, nullptr, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_param_config(port, &config);
    if (err != ESP_OK) {
        uart_driver_delete(port);
        return err;
    }

    err = uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        uart_driver_delete(port);
        return err;
    }

    out_state->initialized = true;
    out_state->port_number = port_number;
    out_state->rx_pin = rx_pin;
    out_state->tx_pin = tx_pin;
    out_state->baud_rate = baud_rate;
    return ESP_OK;
}

esp_err_t UartPortManager::open(
    std::uint8_t port_id,
    std::int16_t rx_pin,
    std::int16_t tx_pin,
    std::uint32_t baud_rate) {
    if (rx_pin < 0 || tx_pin < 0 || baud_rate == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    PortState* port_state = nullptr;
    return ensurePort(port_id, rx_pin, tx_pin, baud_rate, port_state);
}

int UartPortManager::read(
    std::uint8_t port_id,
    std::uint8_t* buffer,
    std::size_t buffer_size,
    TickType_t timeout_ticks) {
    if (buffer == nullptr || buffer_size == 0U || port_id == 0U || port_id > ports_.size()) {
        return -1;
    }

    PortState& port = ports_[port_id - 1U];
    if (!port.initialized) {
        return -1;
    }

    return uart_read_bytes(
        static_cast<uart_port_t>(port.port_number),
        buffer,
        static_cast<uint32_t>(buffer_size),
        timeout_ticks);
}

esp_err_t UartPortManager::flush(std::uint8_t port_id) {
    if (port_id == 0U || port_id > ports_.size()) {
        return ESP_ERR_INVALID_ARG;
    }

    PortState& port = ports_[port_id - 1U];
    if (!port.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return uart_flush_input(static_cast<uart_port_t>(port.port_number));
}

void UartPortManager::shutdown() {
    for (auto& port : ports_) {
        if (!port.initialized) {
            continue;
        }

        uart_driver_delete(static_cast<uart_port_t>(port.port_number));
        port = PortState{};
    }
}

}  // namespace air360
