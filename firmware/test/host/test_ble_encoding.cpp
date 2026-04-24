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

}  // namespace

int main() {
    testWriteLe16();
    testWriteLe16PreservesTwosComplementLayout();
    testWriteLe24();
    std::cout << "ble_encoding tests passed\n";
    return 0;
}
