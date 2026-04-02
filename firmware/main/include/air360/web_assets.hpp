#pragma once

#include <cstddef>
#include <string_view>

namespace air360 {

struct WebAssetView {
    const char* data = nullptr;
    std::size_t size = 0U;
    const char* content_type = "application/octet-stream";
};

const WebAssetView* findEmbeddedWebAsset(std::string_view asset_path);
const char* webUiStylesHref();
const char* webUiScriptHref();

}  // namespace air360
