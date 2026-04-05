#pragma once

#include <cstdint>

namespace air360 {

class I2cBusManager;

void sps30HalSetContext(I2cBusManager* bus_manager, std::uint8_t bus_id);
void sps30HalClearContext();

}  // namespace air360
