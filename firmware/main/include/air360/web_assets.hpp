#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace air360 {

struct WebAssetView {
    const char* data = nullptr;
    std::size_t size = 0U;
    const char* content_type = "application/octet-stream";
};

const WebAssetView* findEmbeddedWebAsset(std::string_view asset_path);
std::string webUiStylesHref();
std::string webUiScriptHref();

}  // namespace air360
