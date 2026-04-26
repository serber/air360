#pragma once

#include "i2cdev.h"

namespace air360 {

class SensirionI2cContextGuard final {
  public:
    explicit SensirionI2cContextGuard(i2c_dev_t* device);
    ~SensirionI2cContextGuard();

    SensirionI2cContextGuard(const SensirionI2cContextGuard&) = delete;
    SensirionI2cContextGuard& operator=(const SensirionI2cContextGuard&) = delete;

  private:
    bool locked_ = false;
};

}  // namespace air360
