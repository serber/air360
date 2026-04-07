#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Arduino.h"
#include "air360/sensors/transport_binding.hpp"

class TwoWire {
  public:
    void attach(air360::I2cBusManager* manager, std::uint8_t bus_id) {
        manager_ = manager;
        bus_id_ = bus_id;
        tx_length_ = 0U;
        rx_length_ = 0U;
        rx_index_ = 0U;
        pending_register_valid_ = false;
    }

    bool begin() {
        return manager_ != nullptr;
    }

    bool begin(int, int) {
        return begin();
    }

    void end() {}

    void setClock(std::uint32_t) {}

    void beginTransmission(std::uint8_t address) {
        current_address_ = address;
        tx_length_ = 0U;
        pending_register_valid_ = false;
    }

    std::size_t write(std::uint8_t value) {
        if (tx_length_ >= tx_buffer_.size()) {
            return 0U;
        }

        tx_buffer_[tx_length_++] = value;
        return 1U;
    }

    std::size_t write(const std::uint8_t* buffer, std::size_t size) {
        if (buffer == nullptr || size == 0U) {
            return 0U;
        }

        const std::size_t remaining = tx_buffer_.size() - tx_length_;
        const std::size_t copy_size = size < remaining ? size : remaining;
        for (std::size_t index = 0; index < copy_size; ++index) {
            tx_buffer_[tx_length_ + index] = buffer[index];
        }
        tx_length_ += copy_size;
        return copy_size;
    }

    std::uint8_t endTransmission(bool = true) {
        if (manager_ == nullptr || tx_length_ == 0U) {
            return 4U;
        }

        if (tx_length_ == 1U) {
            pending_register_ = tx_buffer_[0];
            pending_register_valid_ = true;
            pending_address_ = current_address_;
            return 0U;
        }

        const esp_err_t err = manager_->write(
            bus_id_,
            current_address_,
            tx_buffer_[0],
            tx_buffer_.data() + 1U,
            tx_length_ - 1U);
        pending_register_valid_ = false;
        return err == ESP_OK ? 0U : 4U;
    }

    std::uint8_t requestFrom(std::uint8_t address, std::uint8_t quantity, bool = true) {
        if (manager_ == nullptr || quantity == 0U) {
            rx_length_ = 0U;
            rx_index_ = 0U;
            return 0U;
        }

        const std::size_t read_size =
            quantity < static_cast<std::uint8_t>(rx_buffer_.size()) ? quantity : rx_buffer_.size();

        esp_err_t err = ESP_ERR_INVALID_STATE;
        if (pending_register_valid_ && pending_address_ == address) {
            err = manager_->readRegister(
                bus_id_,
                address,
                pending_register_,
                rx_buffer_.data(),
                read_size);
        } else {
            err = manager_->readRaw(bus_id_, address, rx_buffer_.data(), read_size);
        }

        pending_register_valid_ = false;
        if (err != ESP_OK) {
            rx_length_ = 0U;
            rx_index_ = 0U;
            return 0U;
        }

        rx_length_ = read_size;
        rx_index_ = 0U;
        return static_cast<std::uint8_t>(rx_length_);
    }

    int read() {
        if (rx_index_ >= rx_length_) {
            return -1;
        }

        return rx_buffer_[rx_index_++];
    }

  private:
    air360::I2cBusManager* manager_ = nullptr;
    std::uint8_t bus_id_ = 0U;
    std::uint8_t current_address_ = 0U;
    std::uint8_t pending_address_ = 0U;
    std::uint8_t pending_register_ = 0U;
    bool pending_register_valid_ = false;
    std::array<std::uint8_t, 32> tx_buffer_{};
    std::size_t tx_length_ = 0U;
    std::array<std::uint8_t, 32> rx_buffer_{};
    std::size_t rx_length_ = 0U;
    std::size_t rx_index_ = 0U;
};

inline TwoWire Wire{};
