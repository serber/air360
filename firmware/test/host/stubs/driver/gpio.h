#pragma once
// Type-only stand-in: host tests never touch GPIO hardware.
typedef enum { GPIO_NUM_NC = -1, GPIO_NUM_0 = 0, GPIO_NUM_MAX = 49 } gpio_num_t;
