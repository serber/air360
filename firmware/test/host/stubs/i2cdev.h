#pragma once
// Type-only stand-in for esp-idf-lib i2cdev.
struct i2c_dev_t {
    int port = 0;
    unsigned char addr = 0;
};
