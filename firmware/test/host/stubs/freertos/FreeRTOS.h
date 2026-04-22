#pragma once

#include <cstdint>

using BaseType_t = int;
using EventBits_t = std::uint32_t;
using StackType_t = std::uint32_t;
using TickType_t = std::uint32_t;
using UBaseType_t = unsigned int;

constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdPASS = 1;
constexpr TickType_t portMAX_DELAY = 0xffffffffU;

#ifndef BIT0
#define BIT0 (1U << 0U)
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#endif
