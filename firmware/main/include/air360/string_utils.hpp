#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace air360 {

inline bool isNullTerminated(const char* value, std::size_t capacity) {
    if (value == nullptr || capacity == 0U) {
        return false;
    }
    return value[capacity - 1U] == '\0';
}

inline std::string boundedCString(const char* value, std::size_t capacity) {
    if (value == nullptr || capacity == 0U) {
        return "";
    }
    std::size_t length = 0U;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return std::string(value, length);
}

inline void copyString(char* destination, std::size_t destination_size, std::string_view source) {
    if (destination_size == 0U) {
        return;
    }
    const std::size_t count =
        source.size() < (destination_size - 1U) ? source.size() : (destination_size - 1U);
    if (count > 0U) {
        std::memcpy(destination, source.data(), count);
    }
    destination[count] = '\0';
    for (std::size_t index = count + 1U; index < destination_size; ++index) {
        destination[index] = '\0';
    }
}

inline std::string jsonEscape(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const char ch : input) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"':  escaped += "\\\""; break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:   escaped.push_back(ch); break;
        }
    }
    return escaped;
}

}  // namespace air360
