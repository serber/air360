#pragma once

#include "freertos/FreeRTOS.h"

struct StaticEventGroup_t {
    int unused = 0;
};

using EventGroupHandle_t = StaticEventGroup_t*;
