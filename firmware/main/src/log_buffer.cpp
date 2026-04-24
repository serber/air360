#include "air360/log_buffer.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace air360 {

namespace {

// Keep enough recent logs in RAM for diagnostics without reserving a noticeable
// chunk of memory on an ESP32-S3 system task budget.
constexpr std::size_t kCapacity = 8192;

portMUX_TYPE g_spinlock = portMUX_INITIALIZER_UNLOCKED;
char g_ring[kCapacity] = {};
std::size_t g_write_pos = 0;
bool g_wrapped = false;
int (*g_original_vprintf)(const char*, va_list) = nullptr;

void appendToRing(const char* text, std::size_t len) {
    portENTER_CRITICAL(&g_spinlock);
    for (std::size_t i = 0; i < len; ++i) {
        g_ring[g_write_pos] = text[i];
        ++g_write_pos;
        if (g_write_pos >= kCapacity) {
            g_write_pos = 0;
            g_wrapped = true;
        }
    }
    portEXIT_CRITICAL(&g_spinlock);
}

int logVprintfHook(const char* fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    const int ret = g_original_vprintf(fmt, args);

    char buf[384];
    const int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (len > 0) {
        const std::size_t bytes = static_cast<std::size_t>(
            len < static_cast<int>(sizeof(buf)) ? len : static_cast<int>(sizeof(buf)) - 1);
        appendToRing(buf, bytes);
    }

    return ret;
}

}  // namespace

void logBufferInstall() {
    g_original_vprintf = esp_log_set_vprintf(logVprintfHook);
}

std::string logBufferGetContents() {
    portENTER_CRITICAL(&g_spinlock);
    const std::size_t pos = g_write_pos;
    const bool wrapped = g_wrapped;
    char snapshot[kCapacity];
    memcpy(snapshot, g_ring, kCapacity);
    portEXIT_CRITICAL(&g_spinlock);

    if (!wrapped) {
        return std::string(snapshot, pos);
    }

    std::string result;
    result.reserve(kCapacity);
    result.append(snapshot + pos, kCapacity - pos);
    result.append(snapshot, pos);
    return result;
}

}  // namespace air360
