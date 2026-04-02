#include "air360/web_assets.hpp"

#include <cstdint>

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

const char* webUiStylesHref() {
    return "/assets/air360.css";
}

const char* webUiScriptHref() {
    return "/assets/air360.js";
}

}  // namespace air360
