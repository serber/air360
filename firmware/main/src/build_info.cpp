#include "air360/build_info.hpp"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_efuse.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_rom_sys.h"
#include "rom/ets_sys.h"
#include "soc/efuse_defs.h"
#include "soc/soc_caps.h"

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

std::string chipRevisionLabel(std::uint16_t revision) {
    const unsigned int major = revision / 100U;
    const unsigned int minor = revision % 100U;

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "v%u.%u", major, minor);
    return buffer;
}

std::string chipPackageName(esp_chip_model_t model) {
    switch (model) {
#if CONFIG_IDF_TARGET_ESP32S3
        case CHIP_ESP32S3: {
            switch (esp_efuse_get_pkg_ver()) {
                case EFUSE_PKG_VERSION_ESP32S3:
                    return "QFN56";
                case EFUSE_PKG_VERSION_ESP32S3PICO:
                    return "LGA56";
                default:
                    return "";
            }
        }
#endif
        default:
            return "";
    }
}

void appendFeature(std::string& features, const std::string& feature) {
    if (feature.empty()) {
        return;
    }
    if (!features.empty()) {
        features += ", ";
    }
    features += feature;
}

std::string formatMemorySizeLabel(std::size_t bytes) {
    if (bytes == 0U) {
        return "";
    }

    if ((bytes % (1024U * 1024U)) == 0U) {
        return std::to_string(bytes / (1024U * 1024U)) + "MB";
    }

    if ((bytes % 1024U) == 0U) {
        return std::to_string(bytes / 1024U) + "KB";
    }

    return std::to_string(bytes) + "B";
}

std::string chipTypeLabel(const esp_chip_info_t& chip_info) {
    std::string label = chipModelName(chip_info.model);
    const std::string package_name = chipPackageName(chip_info.model);
    if (!package_name.empty()) {
        label += " (";
        label += package_name;
        label += ")";
    }
    label += " (revision ";
    label += chipRevisionLabel(chip_info.revision);
    label += ")";
    return label;
}

std::string chipFeaturesLabel(const esp_chip_info_t& chip_info) {
    std::string features;

    if ((chip_info.features & CHIP_FEATURE_WIFI_BGN) != 0U) {
        appendFeature(features, "Wi-Fi");
    }

    const bool has_ble = (chip_info.features & CHIP_FEATURE_BLE) != 0U;
    const bool has_bt_classic = (chip_info.features & CHIP_FEATURE_BT) != 0U;
    if (has_ble && has_bt_classic) {
        appendFeature(features, "BT Classic + LE");
    } else if (has_ble) {
        appendFeature(features, "BT LE");
    } else if (has_bt_classic) {
        appendFeature(features, "BT Classic");
    }

    if (chip_info.cores == 2U) {
        appendFeature(features, "Dual Core");
    } else if (chip_info.cores == 1U) {
        appendFeature(features, "Single Core");
    } else if (chip_info.cores > 0U) {
        appendFeature(features, std::to_string(chip_info.cores) + " cores");
    }

#if SOC_LP_CORE_SUPPORTED
    appendFeature(features, "LP Core");
#elif SOC_RISCV_COPROC_SUPPORTED
    appendFeature(features, "ULP RISC-V");
#elif SOC_ULP_SUPPORTED
    appendFeature(features, "ULP");
#endif

    const std::uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();
    if (cpu_mhz > 0U) {
        appendFeature(features, std::to_string(cpu_mhz) + "MHz");
    }

    #if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        const std::string psram_size = formatMemorySizeLabel(esp_psram_get_size());
        if (!psram_size.empty()) {
            appendFeature(features, "PSRAM " + psram_size);
        }
    } else
    #endif
    if ((chip_info.features & CHIP_FEATURE_EMB_PSRAM) != 0U) {
        appendFeature(features, "Embedded PSRAM");
    }

    if ((chip_info.features & CHIP_FEATURE_EMB_FLASH) != 0U) {
        appendFeature(features, "Embedded Flash");
    }

    return features;
}

std::string crystalFrequencyLabel() {
    const std::uint32_t xtal_frequency = ets_get_xtal_freq();
    if (xtal_frequency == 0U) {
        return "";
    }

    std::uint32_t xtal_mhz = xtal_frequency;
    if (xtal_mhz >= 1000000U) {
        xtal_mhz /= 1000000U;
    } else if (xtal_mhz >= 1000U) {
        xtal_mhz /= 1000U;
    }

    return std::to_string(xtal_mhz) + "MHz";
}

std::string formatDecimalDeviceId(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value = (value << 8U) | bytes[index];
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return buffer;
}

std::string formatShortDeviceId(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value = (value << 8U) | bytes[index];
    }

    // Legacy airrohr-style device id uses a shorter numeric identifier.
    value &= 0xFFFFFFULL;

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return buffer;
}

std::string readDeviceId() {
    std::uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return "";
    }

    return formatDecimalDeviceId(mac, sizeof(mac));
}

std::string readShortDeviceId() {
    std::uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return "";
    }

    return formatShortDeviceId(mac, sizeof(mac));
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
    build_info.chip_type = chipTypeLabel(chip_info);
    build_info.chip_features = chipFeaturesLabel(chip_info);
    build_info.crystal_frequency = crystalFrequencyLabel();
    build_info.device_id = readDeviceId();
    build_info.short_device_id = readShortDeviceId();
    build_info.esp_mac_id = readStationMacId();
    return build_info;
}

}  // namespace air360
