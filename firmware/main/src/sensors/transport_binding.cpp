#include "air360/sensors/transport_binding.hpp"

#include <cstddef>
#include <cstdint>

#include "driver/uart.h"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.transport";
constexpr int kI2cTransferTimeoutMs = 200;
constexpr std::uint32_t kI2cClockHz = 100000U;
constexpr int kUartRxBufferSize = 4096;
constexpr int kUartTxBufferSize = 0;
constexpr gpio_num_t kBus0Sda = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SDA_GPIO);
constexpr gpio_num_t kBus0Scl = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SCL_GPIO);
#if CONFIG_ESP_CONSOLE_UART && CONFIG_ESP_CONSOLE_UART_DEFAULT
constexpr int kDefaultConsoleRxPin = 44;
constexpr int kDefaultConsoleTxPin = 43;
#endif

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
    bus_config.trans_queue_depth = 0;
    bus_config.flags.enable_internal_pullup = 1;
    bus_config.flags.allow_pd = 0;

    esp_err_t err = i2c_new_master_bus(&bus_config, &out_state->handle);
    if (err != ESP_OK) {
        return err;
    }

    out_state->initialized = true;
    return ESP_OK;
}

esp_err_t I2cBusManager::ensureDevice(
    std::uint8_t bus_id,
    std::uint8_t address,
    BusState*& out_bus,
    i2c_master_dev_handle_t& out_device) {
    out_bus = nullptr;
    out_device = nullptr;

    esp_err_t err = ensureBus(bus_id, out_bus);
    if (err != ESP_OK) {
        return err;
    }

    for (auto& device : out_bus->devices) {
        if (!device.initialized) {
            continue;
        }
        if (device.address == address && device.handle != nullptr) {
            out_device = device.handle;
            return ESP_OK;
        }
    }

    for (auto& device : out_bus->devices) {
        if (device.initialized) {
            continue;
        }

        i2c_device_config_t device_config{};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = address;
        device_config.scl_speed_hz = kI2cClockHz;
        device_config.scl_wait_us = 0;
        device_config.flags.disable_ack_check = 0;

        err = i2c_master_bus_add_device(out_bus->handle, &device_config, &device.handle);
        if (err != ESP_OK) {
            return err;
        }

        device.initialized = true;
        device.address = address;
        out_device = device.handle;
        return ESP_OK;
    }

    return ESP_ERR_NO_MEM;
}

void I2cBusManager::releaseDevices(BusState& bus) {
    for (auto& device : bus.devices) {
        if (!device.initialized || device.handle == nullptr) {
            continue;
        }

        i2c_master_bus_rm_device(device.handle);
        device.handle = nullptr;
        device.address = 0U;
        device.initialized = false;
    }
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

    err = i2c_master_probe(bus_state->handle, address, kI2cTransferTimeoutMs);
    unlock();
    return err;
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
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = ensureDevice(bus_id, address, bus_state, device);
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

    err = i2c_master_transmit(device, staging, buffer_size + 1U, kI2cTransferTimeoutMs);
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
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = ensureDevice(bus_id, address, bus_state, device);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = i2c_master_transmit(device, buffer, buffer_size, kI2cTransferTimeoutMs);
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
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = ensureDevice(bus_id, address, bus_state, device);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = i2c_master_receive(device, buffer, buffer_size, kI2cTransferTimeoutMs);
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
    i2c_master_dev_handle_t device = nullptr;
    esp_err_t err = ensureDevice(bus_id, address, bus_state, device);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = i2c_master_transmit_receive(
        device,
        &reg,
        1U,
        buffer,
        buffer_size,
        kI2cTransferTimeoutMs);
    unlock();
    return err;
}

esp_err_t I2cBusManager::getComponentBus(
    std::uint8_t bus_id,
    i2c_bus_handle_t& out_handle) {
    out_handle = nullptr;

    ensureMutex();
    lock();
    BusState* bus_state = nullptr;
    esp_err_t err = ensureBus(bus_id, bus_state);
    unlock();
    if (err != ESP_OK) {
        return err;
    }

    i2c_port_num_t port = I2C_NUM_0;
    gpio_num_t sda_pin = GPIO_NUM_NC;
    gpio_num_t scl_pin = GPIO_NUM_NC;
    if (!resolveBusPins(bus_id, port, sda_pin, scl_pin)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    i2c_config_t config{};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = sda_pin;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_io_num = scl_pin;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = kI2cClockHz;

    out_handle = i2c_bus_create(static_cast<i2c_port_t>(port), &config);
    return out_handle != nullptr ? ESP_OK : ESP_FAIL;
}

void I2cBusManager::shutdown() {
    ensureMutex();
    lock();
    for (auto& bus : buses_) {
        if (!bus.initialized || bus.handle == nullptr) {
            continue;
        }

        releaseDevices(bus);
        i2c_del_master_bus(bus.handle);
        bus.handle = nullptr;
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

#if CONFIG_ESP_CONSOLE_UART && CONFIG_ESP_CONSOLE_UART_DEFAULT
    if (port_number != CONFIG_ESP_CONSOLE_UART_NUM &&
        ((rx_pin == kDefaultConsoleRxPin && tx_pin == kDefaultConsoleTxPin) ||
         (rx_pin == kDefaultConsoleTxPin && tx_pin == kDefaultConsoleRxPin))) {
        ESP_LOGW(
            kTag,
            "UART%u is being mapped to GPIO %d/%d, which overlap the default console pins; serial logs may disappear after sensor init",
            static_cast<unsigned>(port_id),
            static_cast<int>(rx_pin),
            static_cast<int>(tx_pin));
    }
#endif

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
