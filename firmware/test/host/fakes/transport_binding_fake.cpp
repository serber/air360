// Host replacement for main/src/sensors/transport_binding.cpp. Bus managers
// own no hardware here; they only count lifecycle calls so tests can assert
// that SensorManager releases UART ports on stop().
#include "air360/sensors/transport_binding.hpp"

#include "fake_transport.hpp"

namespace air360 {

esp_err_t I2cBusManager::init() {
    ++fake_transport::i2c_init_calls;
    return ESP_OK;
}

bool I2cBusManager::resolvePins(std::uint8_t, i2c_port_t& out_port, gpio_num_t& out_sda,
                                gpio_num_t& out_scl) const {
    out_port = 0;
    out_sda = GPIO_NUM_NC;
    out_scl = GPIO_NUM_NC;
    return true;
}

esp_err_t I2cBusManager::setupDevice(const SensorRecord&, std::uint32_t, i2c_dev_t&) const {
    return ESP_OK;
}

void I2cBusManager::applyDescriptorDefaults(i2c_dev_t&, std::uint32_t) const {}

esp_err_t I2cBusManager::getMasterBusHandle(std::uint8_t, i2c_master_bus_handle_t& out) const {
    out = nullptr;
    return ESP_OK;
}

UartPortManager::~UartPortManager() {
    shutdown();
}

esp_err_t UartPortManager::open(std::uint8_t port_id, std::int16_t, std::int16_t, std::uint32_t,
                                std::size_t, std::size_t) {
    if (port_id == 0U || static_cast<int>(port_id) >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    ports_[port_id].initialized = true;
    return ESP_OK;
}

int UartPortManager::read(std::uint8_t, std::uint8_t* buffer, std::size_t buffer_size, TickType_t) {
    std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
    std::size_t count = 0U;
    while (count < buffer_size && !fake_transport::rx.empty()) {
        buffer[count++] = fake_transport::rx.front();
        fake_transport::rx.pop_front();
    }
    return static_cast<int>(count);
}

int UartPortManager::write(std::uint8_t, const std::uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
    fake_transport::writes.emplace_back(data, data + size);
    return static_cast<int>(size);
}

esp_err_t UartPortManager::bufferedDataLength(std::uint8_t, std::size_t& out_length) const {
    std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
    out_length = fake_transport::rx.size();
    return ESP_OK;
}

esp_err_t UartPortManager::drainEvents(std::uint8_t, EventSummary& out_summary) {
    std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
    out_summary = EventSummary{};
    out_summary.overrun_count = fake_transport::pending_overruns;
    fake_transport::pending_overruns = 0U;
    return ESP_OK;
}

esp_err_t UartPortManager::flush(std::uint8_t) {
    std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
    fake_transport::rx.clear();
    return ESP_OK;
}

void UartPortManager::shutdown() {
    for (auto& port : ports_) {
        if (port.initialized) {
            ++fake_transport::uart_ports_released;
        }
        port = PortState{};
    }
    ++fake_transport::uart_shutdown_calls;
}

}  // namespace air360
