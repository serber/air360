#pragma once

#include "i2cdev.h"

namespace air360 {

void sps30HalSetContext(i2c_dev_t* device);
void sps30HalClearContext();

}  // namespace air360
