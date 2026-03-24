#include "air360/build_info.hpp"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_mac.h"

namespace air360 {

namespace {

std::string formatHexBytes(const std::uint8_t* bytes, std::size_t size) {
    std::string value;
    value.resize(size * 2U);

    for (std::size_t index = 0; index < size; ++index) {
        std::snprintf(
            value.data() + (index * 2U),
            3U,
            "%02x",
            static_cast<unsigned int>(bytes[index]));
    }

    return value;
}

const char* chipModelName(esp_chip_model_t model) {
    switch (model) {
        case CHIP_ESP32:
            return "ESP32";
        case CHIP_ESP32S2:
            return "ESP32-S2";
        case CHIP_ESP32S3:
            return "ESP32-S3";
        case CHIP_ESP32C2:
            return "ESP32-C2";
        case CHIP_ESP32C3:
            return "ESP32-C3";
        case CHIP_ESP32C5:
            return "ESP32-C5";
        case CHIP_ESP32C6:
            return "ESP32-C6";
        case CHIP_ESP32C61:
            return "ESP32-C61";
        case CHIP_ESP32H2:
            return "ESP32-H2";
        case CHIP_ESP32H21:
            return "ESP32-H21";
        case CHIP_ESP32H4:
            return "ESP32-H4";
        case CHIP_ESP32P4:
            return "ESP32-P4";
        case CHIP_POSIX_LINUX:
            return "POSIX/Linux";
        default:
            return "Unknown";
    }
}

std::string formatDecimalChipId(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value = (value << 8U) | bytes[index];
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return buffer;
}

std::string formatShortChipId(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value = (value << 8U) | bytes[index];
    }

    // Legacy airrohr-style chip id uses a shorter numeric identifier.
    value &= 0xFFFFFFULL;

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return buffer;
}

std::string readChipId() {
    std::uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return "";
    }

    return formatDecimalChipId(mac, sizeof(mac));
}

std::string readShortChipId() {
    std::uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return "";
    }

    return formatShortChipId(mac, sizeof(mac));
}

std::string readStationMacId() {
    std::uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "";
    }

    return formatHexBytes(mac, sizeof(mac));
}

}  // namespace

BuildInfo getBuildInfo() {
    const esp_app_desc_t* app = esp_app_get_description();
    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);

    BuildInfo build_info;
    build_info.project_name = app->project_name;
    build_info.project_version = app->version;
    build_info.idf_version = esp_get_idf_version();
    build_info.compile_date = app->date;
    build_info.compile_time = app->time;
    build_info.board_name = CONFIG_AIR360_BOARD_NAME;
    build_info.chip_name = chipModelName(chip_info.model);
    build_info.chip_revision = std::to_string(chip_info.revision);
    build_info.chip_id = readChipId();
    build_info.short_chip_id = readShortChipId();
    build_info.esp_mac_id = readStationMacId();
    return build_info;
}

}  // namespace air360
