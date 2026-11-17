// UART frame handling of the SDS011 and PMSX003 drivers against a scripted
// UART (fakes/transport_binding_fake.cpp). PMSX003 checksum/field decoding is
// delegated to the esp-pms component, modeled by stubs/pms.h; frame sync,
// resync and freshness are the driver's own code and are what this covers.
#include "air360/sensors/drivers/pmsx003_sensor.hpp"
#include "air360/sensors/drivers/sds011_sensor.hpp"
#include "air360/sensors/transport_binding.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "fake_transport.hpp"
#include "pms.h"

namespace {

using air360::SensorMeasurement;
using air360::SensorValueKind;
using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

float valueOf(const SensorMeasurement& measurement, SensorValueKind kind) {
    const auto* value = measurement.findValue(kind);
    return value == nullptr ? -1.0F : value->value;
}

air360::SensorRecord uartRecord(air360::SensorType type, std::uint32_t baud) {
    air360::SensorRecord record{};
    record.id = 1U;
    record.enabled = 1U;
    record.sensor_type = type;
    record.transport_kind = air360::TransportKind::kUart;
    record.uart_port_id = 2U;
    record.uart_rx_gpio_pin = 16;
    record.uart_tx_gpio_pin = 15;
    record.uart_baud_rate = baud;
    record.poll_interval_ms = 60000U;
    return record;
}

// ── SDS011 ──────────────────────────────────────────────────────────────
// Measurement frame: AA C0 pm25_lo pm25_hi pm10_lo pm10_hi id1 id2 sum AB,
// values in 0.1 µg/m³, sum = bytes 2..7 mod 256.
Bytes sdsFrame(std::uint16_t pm25_tenths, std::uint16_t pm10_tenths) {
    Bytes frame{0xAA, 0xC0,
                static_cast<std::uint8_t>(pm25_tenths & 0xFF), static_cast<std::uint8_t>(pm25_tenths >> 8),
                static_cast<std::uint8_t>(pm10_tenths & 0xFF), static_cast<std::uint8_t>(pm10_tenths >> 8),
                0x12, 0x34, 0x00, 0xAB};
    std::uint8_t sum = 0U;
    for (std::size_t index = 2U; index <= 7U; ++index) {
        sum = static_cast<std::uint8_t>(sum + frame[index]);
    }
    frame[8] = sum;
    return frame;
}

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

void testSds011Commands(air360::UartPortManager& uart) {
    fake_transport::resetUart();
    air360::Sds011Sensor sensor;
    require(sensor.init(uartRecord(air360::SensorType::kSds011, 9600U), {nullptr, &uart}) == ESP_OK,
            "SDS011 init");
    std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
    require(fake_transport::writes.size() == 3U, "init sends wake, work period, report mode");
    for (const auto& command : fake_transport::writes) {
        require(command.size() == 19U && command[0] == 0xAA && command[1] == 0xB4 &&
                    command[18] == 0xAB,
                "command framing");
        std::uint8_t sum = 0U;
        for (std::size_t index = 2U; index <= 16U; ++index) {
            sum = static_cast<std::uint8_t>(sum + command[index]);
        }
        require(command[17] == sum, "command checksum");
    }
    require(fake_transport::writes[2][2] == 0x02 && fake_transport::writes[2][4] == 0x01,
            "passive report mode requested");
}

void testSds011Frames(air360::UartPortManager& uart) {
    fake_transport::resetUart();
    air360::Sds011Sensor sensor;
    require(sensor.init(uartRecord(air360::SensorType::kSds011, 9600U), {nullptr, &uart}) == ESP_OK,
            "init");

    // Empty read: success, no measurement, explicit status.
    require(sensor.poll() == ESP_OK, "empty poll succeeds");
    require(sensor.latestMeasurement().empty(), "no data -> no measurement");
    require(sensor.lastError() == "No SDS011 UART data received.", "empty status");

    // Valid frame after line noise (including a lone 0xAA) is found.
    fake_transport::queueRx(concat({{0x00, 0xFF, 0xAA, 0x13}, sdsFrame(125, 340)}));
    require(sensor.poll() == ESP_OK, "poll with frame");
    SensorMeasurement first = sensor.latestMeasurement();
    require(valueOf(first, SensorValueKind::kPm2_5UgM3) == 12.5F &&
                valueOf(first, SensorValueKind::kPm10_0UgM3) == 34.0F,
            "frame decoded after resync");
    require(air360::isFreshSample(first, 0U), "decoded frame is a fresh sample");

    // Bad checksum: rejected, previous sample kept and not fresh.
    Bytes corrupt = sdsFrame(999, 999);
    corrupt[8] ^= 0x01;
    fake_transport::queueRx(corrupt);
    require(sensor.poll() == ESP_OK, "poll with corrupt frame");
    require(sensor.lastError() == "No valid SDS011 frame received.", "corrupt frame status");
    require(sensor.latestMeasurement().sample_time_ms == first.sample_time_ms &&
                !air360::isFreshSample(sensor.latestMeasurement(), first.sample_time_ms),
            "corrupt frame does not produce a fresh sample");

    // Several frames in one read: the latest wins.
    fake_transport::queueRx(concat({sdsFrame(10, 20), sdsFrame(30, 40)}));
    require(sensor.poll() == ESP_OK, "poll with two frames");
    require(valueOf(sensor.latestMeasurement(), SensorValueKind::kPm2_5UgM3) == 3.0F,
            "latest frame of a burst is used");

    // Frame split across polls is reassembled.
    const Bytes split = sdsFrame(555, 777);
    fake_transport::queueRx(Bytes(split.begin(), split.begin() + 4));
    const std::uint64_t before_split = sensor.latestMeasurement().sample_time_ms;
    require(sensor.poll() == ESP_OK, "first half");
    require(sensor.latestMeasurement().sample_time_ms == before_split, "half frame -> no sample");
    fake_transport::queueRx(Bytes(split.begin() + 4, split.end()));
    require(sensor.poll() == ESP_OK, "second half");
    require(valueOf(sensor.latestMeasurement(), SensorValueKind::kPm2_5UgM3) == 55.5F,
            "split frame reassembled");

    // A UART overrun discards the partial frame.
    fake_transport::queueRx(Bytes(split.begin(), split.begin() + 5));
    require(sensor.poll() == ESP_OK, "partial before overrun");
    {
        std::lock_guard<std::mutex> guard(fake_transport::uart_mutex);
        fake_transport::pending_overruns = 1U;
    }
    fake_transport::queueRx(Bytes(split.begin() + 5, split.end()));
    const std::uint64_t before_overrun = sensor.latestMeasurement().sample_time_ms;
    require(sensor.poll() == ESP_OK, "poll after overrun");
    require(sensor.latestMeasurement().sample_time_ms == before_overrun,
            "tail after an overrun is not glued to the old head");
}

// ── PMSX003 ─────────────────────────────────────────────────────────────
// 32-byte frame: 42 4D 00 1C, 13 big-endian fields, checksum over bytes 0..29.
Bytes pmsFrame(std::uint16_t pm25_atm, std::uint16_t pm10_atm) {
    Bytes frame(32U, 0U);
    frame[0] = 0x42;
    frame[1] = 0x4D;
    frame[2] = 0x00;
    frame[3] = 0x1C;
    const auto put = [&frame](int field, std::uint16_t value) {
        frame[4 + 2 * field] = static_cast<std::uint8_t>(value >> 8);
        frame[5 + 2 * field] = static_cast<std::uint8_t>(value & 0xFF);
    };
    put(PMS_FIELD_PM1_ATM, 7);
    put(PMS_FIELD_PM2_5_ATM, pm25_atm);
    put(PMS_FIELD_PM10_ATM, pm10_atm);
    put(PMS_FIELD_PC_0_3, 1500);
    std::uint16_t sum = 0U;
    for (std::size_t index = 0U; index < 30U; ++index) {
        sum = static_cast<std::uint16_t>(sum + frame[index]);
    }
    frame[30] = static_cast<std::uint8_t>(sum >> 8);
    frame[31] = static_cast<std::uint8_t>(sum & 0xFF);
    return frame;
}

void testPmsx003Frames(air360::UartPortManager& uart) {
    fake_transport::resetUart();
    pms_test::state = PMS_STATE_ACTIVE;
    air360::Pmsx003Sensor sensor;
    require(sensor.init(uartRecord(air360::SensorType::kPmsx003, 9600U), {nullptr, &uart}) == ESP_OK,
            "PMSX003 init");

    // Not active yet: warming up, nothing read.
    pms_test::state = PMS_STATE_STABILIZING;
    fake_transport::queueRx(pmsFrame(11, 22));
    require(sensor.poll() == ESP_OK && sensor.latestMeasurement().empty(),
            "no measurement while the sensor stabilizes");
    require(sensor.lastError() == "PMSX003 is warming up.", "warmup status");
    pms_test::state = PMS_STATE_ACTIVE;

    // Valid frame after noise, including a false head (0x42 0x00).
    fake_transport::resetUart();
    fake_transport::queueRx(concat({{0x42, 0x00, 0x13, 0x42}, pmsFrame(18, 27)}));
    require(sensor.poll() == ESP_OK, "poll with frame");
    const SensorMeasurement first = sensor.latestMeasurement();
    require(valueOf(first, SensorValueKind::kPm2_5UgM3) == 18.0F &&
                valueOf(first, SensorValueKind::kPm10_0UgM3) == 27.0F &&
                valueOf(first, SensorValueKind::kPm1_0UgM3) == 7.0F &&
                valueOf(first, SensorValueKind::kPc0_3Per0_1L) == 1500.0F,
            "PMSX003 frame decoded after resync");

    // Wrong length bytes: the frame is dropped at byte 3.
    Bytes bad_length = pmsFrame(99, 99);
    bad_length[3] = 0x14;
    fake_transport::queueRx(bad_length);
    require(sensor.poll() == ESP_OK, "poll with bad length");
    require(sensor.latestMeasurement().sample_time_ms == first.sample_time_ms,
            "frame with unexpected length is ignored");

    // Bad checksum: rejected by the component, no fresh sample.
    Bytes corrupt = pmsFrame(99, 99);
    corrupt[31] ^= 0x01;
    fake_transport::queueRx(corrupt);
    require(sensor.poll() == ESP_OK, "poll with corrupt frame");
    require(sensor.lastError() == "No valid PMSX003 frame received.", "corrupt frame status");
    require(!air360::isFreshSample(sensor.latestMeasurement(), first.sample_time_ms),
            "corrupt frame does not produce a fresh sample");

    // Split across polls.
    const Bytes split = pmsFrame(41, 52);
    fake_transport::queueRx(Bytes(split.begin(), split.begin() + 10));
    require(sensor.poll() == ESP_OK, "first part");
    fake_transport::queueRx(Bytes(split.begin() + 10, split.end()));
    require(sensor.poll() == ESP_OK, "second part");
    require(valueOf(sensor.latestMeasurement(), SensorValueKind::kPm2_5UgM3) == 41.0F,
            "split PMSX003 frame reassembled");

    // No data: explicit status, cached sample not refreshed.
    const std::uint64_t cached = sensor.latestMeasurement().sample_time_ms;
    require(sensor.poll() == ESP_OK, "empty poll");
    require(sensor.lastError() == "No PMSX003 UART data received." &&
                sensor.latestMeasurement().sample_time_ms == cached,
            "empty read keeps the cached sample unchanged");
}

}  // namespace

int main() {
    air360::UartPortManager uart;
    testSds011Commands(uart);
    testSds011Frames(uart);
    testPmsx003Frames(uart);
    std::cout << "uart frame tests passed\n";
    return 0;
}
