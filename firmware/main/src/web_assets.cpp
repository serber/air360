#include "air360/web_assets.hpp"

#include <cstdint>
#include <string>

#include "esp_app_desc.h"

namespace air360 {

namespace {

extern const std::uint8_t air360_css_start[] asm("_binary_air360_css_start");
extern const std::uint8_t air360_css_end[] asm("_binary_air360_css_end");
extern const std::uint8_t air360_js_start[] asm("_binary_air360_js_start");
extern const std::uint8_t air360_js_end[] asm("_binary_air360_js_end");

std::size_t embeddedTextSize(const std::uint8_t* start, const std::uint8_t* end) {
    if (start == nullptr || end == nullptr || end <= start) {
        return 0U;
    }

    std::size_t size = static_cast<std::size_t>(end - start);
    if (size > 0U && end[-1] == '\0') {
        --size;
    }
    return size;
}

const WebAssetView kStylesAsset{
    reinterpret_cast<const char*>(air360_css_start),
    embeddedTextSize(air360_css_start, air360_css_end),
    "text/css; charset=utf-8",
};

const WebAssetView kScriptAsset{
    reinterpret_cast<const char*>(air360_js_start),
    embeddedTextSize(air360_js_start, air360_js_end),
    "application/javascript; charset=utf-8",
};

std::string versionedAssetHref(const char* asset_name) {
    const esp_app_desc_t* app = esp_app_get_description();
    std::string href = "/assets/";
    href += asset_name;

    if (app != nullptr) {
        href += "?v=";
        href += app->version[0] != '\0' ? app->version : "unknown";

        if (app->date[0] != '\0') {
            href += "-";
            for (const char* cursor = app->date; *cursor != '\0'; ++cursor) {
                const char ch = *cursor;
                href.push_back(ch == ' ' ? '_' : ch);
            }
        }

        if (app->time[0] != '\0') {
            href += "-";
            for (const char* cursor = app->time; *cursor != '\0'; ++cursor) {
                const char ch = *cursor;
                href.push_back(ch == ':' ? '-' : ch);
            }
        }
    }

    return href;
}

}  // namespace

const WebAssetView* findEmbeddedWebAsset(std::string_view asset_path) {
    if (asset_path == "air360.css") {
        return &kStylesAsset;
    }
    if (asset_path == "air360.js") {
        return &kScriptAsset;
    }
    return nullptr;
}

std::string webUiStylesHref() {
    return versionedAssetHref("air360.css");
}

std::string webUiScriptHref() {
    return versionedAssetHref("air360.js");
}

}  // namespace air360
