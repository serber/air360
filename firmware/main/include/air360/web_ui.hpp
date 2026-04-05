#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace air360 {

enum class WebPageKey {
    kHome = 1,
    kConfig = 2,
    kSensors = 3,
    kBackends = 4,
};

enum class WebTemplateKey {
    kHome = 1,
    kConfig = 2,
    kSensors = 3,
    kBackends = 4,
    kBackendCard = 5,
    kSensorCard = 6,
    kOverviewBackendItem = 7,
    kOverviewSensorItem = 8,
};

using WebTemplateBindings = std::vector<std::pair<std::string, std::string>>;

std::string htmlEscape(std::string_view input);
std::string renderNotice(const std::string& notice, bool error_notice);
std::string renderTemplate(WebTemplateKey template_key, const WebTemplateBindings& bindings);
std::string renderPageTemplate(WebTemplateKey template_key, const WebTemplateBindings& bindings);
std::string renderPageDocument(
    WebPageKey active_page,
    std::string_view title,
    std::string_view heading,
    std::string_view lead_html,
    std::string_view body_html,
    bool wide_layout = true,
    bool device_only_navigation = false);

}  // namespace air360
