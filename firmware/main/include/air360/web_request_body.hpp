#pragma once

#include <cstddef>
#include <string>

#include "esp_err.h"

namespace air360::web {

inline constexpr std::size_t kHttpMaxRequestBodySize = 4096U;
inline constexpr int kMaxRequestBodyReadTimeouts = 5;

using RequestBodyReceiveFn = int (*)(void* context, char* buffer, std::size_t buffer_size);

esp_err_t readRequestBodyWithRetries(
    std::size_t content_len,
    void* receive_context,
    RequestBodyReceiveFn receive,
    int timeout_result,
    std::string& out_body);

}  // namespace air360::web
