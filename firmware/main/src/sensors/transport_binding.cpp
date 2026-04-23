#include "air360/sensors/transport_binding.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "driver/uart.h"
#include "sdkconfig.h"

namespace air360 {

namespace {

constexpr int kUartTxBufferSize = 0;

constexpr BusConfig kBuses[] = {
    {
        .id       = 0U,
        .sda      = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SDA_GPIO),
        .scl      = static_cast<gpio_num_t>(CONFIG_AIR360_I2C0_SCL_GPIO),
        .clock_hz = 100000U,
    },
};

}  // namespace

// ── I2cBusManager ────────────────────────────────────────────────────────────

esp_err_t I2cBusManager::init() {
    buses_ = kBuses;
    return i2cdev_init();
}

bool I2cBusManager::resolvePins(
    std::uint8_t bus_id,
    i2c_port_t& out_port,
    gpio_num_t& out_sda,
    gpio_num_t& out_scl) const {
    for (const BusConfig& bus : buses_) {
        if (bus.id == bus_id) {
            out_port = static_cast<i2c_port_t>(bus_id);
            out_sda = bus.sda;
            out_scl = bus.scl;
            return true;
        }
    }
    return false;
}

esp_err_t I2cBusManager::setupDevice(
    const SensorRecord& record,
    std::uint32_t speed_hz,
    i2c_dev_t& out_dev) const {
    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!resolvePins(record.i2c_bus_id, port, sda, scl)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&out_dev, 0, sizeof(out_dev));
    out_dev.port = port;
    out_dev.addr = record.i2c_address;
    out_dev.cfg.sda_io_num = sda;
    out_dev.cfg.scl_io_num = scl;
    out_dev.cfg.master.clk_speed = speed_hz;
    out_dev.cfg.sda_pullup_en = 1;
    out_dev.cfg.scl_pullup_en = 1;

    return i2c_dev_create_mutex(&out_dev);
}

esp_err_t I2cBusManager::getComponentBus(
    std::uint8_t bus_id,
    i2c_bus_handle_t& out_handle) const {
    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!resolvePins(bus_id, port, sda, scl)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // i2cdev already owns this bus. i2c_bus_create() detects the existing
    // handle via i2c_master_get_bus_handle() and borrows it.
    const BusConfig* bus_cfg = nullptr;
    for (const BusConfig& bus : buses_) {
        if (bus.id == bus_id) {
            bus_cfg = &bus;
            break;
        }
    }

    i2c_config_t config{};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = sda;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_io_num = scl;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = (bus_cfg != nullptr) ? bus_cfg->clock_hz : 100000U;

    out_handle = i2c_bus_create(port, &config);
    return out_handle != nullptr ? ESP_OK : ESP_FAIL;
}

// ── UartPortManager ──────────────────────────────────────────────────────────

UartPortManager::~UartPortManager() {
    shutdown();
}

esp_err_t UartPortManager::ensurePort(
    std::uint8_t port_id,
    std::int16_t rx_pin,
    std::int16_t tx_pin,
    std::uint32_t baud_rate,
    std::size_t rx_buffer_size,
    std::size_t event_queue_size,
    PortState*& out_state) {
    if (port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (rx_buffer_size == 0U ||
        rx_buffer_size > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        event_queue_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ESP_ERR_INVALID_ARG;
    }

    out_state = &ports_[port_id];
    if (out_state->initialized) {
        if (out_state->rx_pin == rx_pin &&
            out_state->tx_pin == tx_pin &&
            out_state->baud_rate == baud_rate &&
            out_state->rx_buffer_size == rx_buffer_size &&
            ((event_queue_size > 0U) == (out_state->event_queue != nullptr))) {
            return ESP_OK;
        }
        return ESP_ERR_INVALID_STATE;
    }

    const uart_port_t port = static_cast<uart_port_t>(port_id);

    uart_config_t config{};
    config.baud_rate = static_cast<int>(baud_rate);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    QueueHandle_t event_queue = nullptr;
    const int queue_depth = static_cast<int>(event_queue_size);
    esp_err_t err = uart_driver_install(
        port,
        static_cast<int>(rx_buffer_size),
        kUartTxBufferSize,
        queue_depth,
        queue_depth > 0 ? &event_queue : nullptr,
        0);
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
    out_state->port_number = static_cast<int>(port_id);
    out_state->rx_pin = rx_pin;
    out_state->tx_pin = tx_pin;
    out_state->baud_rate = baud_rate;
    out_state->rx_buffer_size = rx_buffer_size;
    out_state->event_queue = event_queue;
    return ESP_OK;
}

esp_err_t UartPortManager::open(
    std::uint8_t port_id,
    std::int16_t rx_pin,
    std::int16_t tx_pin,
    std::uint32_t baud_rate,
    std::size_t rx_buffer_size,
    std::size_t event_queue_size) {
    if (rx_pin < 0 || tx_pin < 0 || baud_rate == 0U || rx_buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    PortState* port_state = nullptr;
    return ensurePort(
        port_id,
        rx_pin,
        tx_pin,
        baud_rate,
        rx_buffer_size,
        event_queue_size,
        port_state);
}

int UartPortManager::read(
    std::uint8_t port_id,
    std::uint8_t* buffer,
    std::size_t buffer_size,
    TickType_t timeout_ticks) {
    if (buffer == nullptr || buffer_size == 0U ||
        port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return -1;
    }

    PortState& port = ports_[port_id];
    if (!port.initialized) {
        return -1;
    }

    return uart_read_bytes(
        static_cast<uart_port_t>(port.port_number),
        buffer,
        static_cast<uint32_t>(buffer_size),
        timeout_ticks);
}

int UartPortManager::write(
    std::uint8_t port_id,
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr || size == 0U ||
        port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return -1;
    }

    PortState& port = ports_[port_id];
    if (!port.initialized) {
        return -1;
    }

    return uart_write_bytes(
        static_cast<uart_port_t>(port.port_number),
        data,
        size);
}

esp_err_t UartPortManager::bufferedDataLength(
    std::uint8_t port_id,
    std::size_t& out_length) const {
    out_length = 0U;
    if (port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    const PortState& port = ports_[port_id];
    if (!port.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    std::size_t buffered_length = 0U;
    const esp_err_t err = uart_get_buffered_data_len(
        static_cast<uart_port_t>(port.port_number),
        &buffered_length);
    if (err != ESP_OK) {
        return err;
    }

    out_length = buffered_length;
    return ESP_OK;
}

esp_err_t UartPortManager::drainEvents(
    std::uint8_t port_id,
    EventSummary& out_summary) {
    out_summary = EventSummary{};
    if (port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    PortState& port = ports_[port_id];
    if (!port.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (port.event_queue == nullptr) {
        return ESP_OK;
    }

    uart_event_t event{};
    while (xQueueReceive(port.event_queue, &event, 0) == pdTRUE) {
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            ++out_summary.overrun_count;
            const esp_err_t flush_err = uart_flush_input(static_cast<uart_port_t>(port.port_number));
            if (flush_err != ESP_OK) {
                return flush_err;
            }
        }
    }

    return ESP_OK;
}

esp_err_t UartPortManager::flush(std::uint8_t port_id) {
    if (port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    PortState& port = ports_[port_id];
    if (!port.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return uart_flush_input(static_cast<uart_port_t>(port.port_number));
}

void UartPortManager::shutdown() {
    for (std::uint8_t port_id = 1U; static_cast<int>(port_id) < UART_NUM_MAX; ++port_id) {
        PortState& port = ports_[port_id];
        if (!port.initialized) {
            continue;
        }

        uart_driver_delete(static_cast<uart_port_t>(port.port_number));
        port = PortState{};
    }
}

}  // namespace air360
