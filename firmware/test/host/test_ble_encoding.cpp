#include "air360/ble_encoding.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void testWriteLe16() {
    std::array<std::uint8_t, 2U> buf{};
    air360::ble::writeLe16(buf.data(), 0x1234U);
    require(buf[0] == 0x34U, "writeLe16 stores low byte first");
    require(buf[1] == 0x12U, "writeLe16 stores high byte second");
}

void testWriteLe16PreservesTwosComplementLayout() {
    std::array<std::uint8_t, 2U> buf{};
    air360::ble::writeLe16(buf.data(), static_cast<std::uint16_t>(static_cast<std::int16_t>(-200)));
    require(buf[0] == 0x38U, "writeLe16 preserves signed payload low byte");
    require(buf[1] == 0xFFU, "writeLe16 preserves signed payload high byte");
}

void testWriteLe24() {
    std::array<std::uint8_t, 3U> buf{};
    air360::ble::writeLe24(buf.data(), 0x123456U);
    require(buf[0] == 0x56U, "writeLe24 stores byte 0");
    require(buf[1] == 0x34U, "writeLe24 stores byte 1");
    require(buf[2] == 0x12U, "writeLe24 stores byte 2");
}

void testAdvertisementBudget() {
    std::array<std::uint8_t, 32U> out{};
    out.back() = 0xAAU;
    // BME280 + SPS30: UUID/device info, four 3-byte readings, pressure (4 bytes).
    const std::array<std::uint8_t, 19U> service{
        0xD2, 0xFC, 0x40, 0x02, 0xCA, 0x08, 0x03, 0x88, 0x13,
        0x0D, 0x64, 0x00, 0x0E, 0xC8, 0x00, 0x04, 0xCD, 0x8B, 0x01};
    const auto size = air360::ble::buildAdvertisement(
        out.data(), 31U, "air360", service.data(), service.size());
    require(size == 31U && out.back() == 0xAAU, "packet respects 31-byte capacity");
    require(out[3] == 20U && out[4] == 0x16U, "service data remains present");
    require(std::memcmp(out.data() + 5U, service.data(), service.size()) == 0,
            "all measurement bytes survive");
    require(out[24] == 6U && out[25] == 0x08U, "name is marked shortened");
    require(std::memcmp(out.data() + 26U, "air36", 5U) == 0, "name uses remainder");

    std::array<std::uint8_t, 26U> full_service{};
    require(air360::ble::buildAdvertisement(out.data(), 31U, "long-device-name",
                full_service.data(), full_service.size()) == 31U,
            "full service data fits without a name");
    require(out[3] == 27U && out[4] == 0x16U, "full service data is retained");
    require(air360::ble::buildAdvertisement(out.data(), 31U, "abc",
                service.data(), service.size()) == 29U && out[25] == 0x09U,
            "short name is marked complete");
    require(air360::ble::buildAdvertisement(out.data(), 23U, "air360",
                service.data(), service.size()) == 0U,
            "undersized output is rejected");
}

}  // namespace

int main() {
    testAdvertisementBudget();
    testWriteLe16();
    testWriteLe16PreservesTwosComplementLayout();
    testWriteLe24();
    std::cout << "ble_encoding tests passed\n";
    return 0;
}
