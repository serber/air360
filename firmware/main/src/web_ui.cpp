#include "air360/web_ui.hpp"

#include <array>
#include <cstdint>

#include "air360/web_assets.hpp"
#include "esp_app_desc.h"

namespace air360 {

namespace {

struct NavItem {
    WebPageKey page;
    const char* href;
    const char* label;
    const char* icon;  // inner content of the nav SVG (24×24 viewBox)
};

// clang-format off
constexpr std::array<NavItem, 5> kNavItems{{
    {WebPageKey::kHome, "/", "Overview",
     "<rect x='3' y='3' width='7' height='9' rx='1.5'/>"
     "<rect x='14' y='3' width='7' height='5' rx='1.5'/>"
     "<rect x='14' y='12' width='7' height='9' rx='1.5'/>"
     "<rect x='3' y='16' width='7' height='5' rx='1.5'/>"},
    {WebPageKey::kConfig, "/config", "Device",
     "<rect x='4' y='3' width='16' height='18' rx='2'/>"
     "<circle cx='12' cy='17' r='1.2'/>"
     "<path d='M8 7h8M8 11h5'/>"},
    {WebPageKey::kSensors, "/sensors", "Sensors",
     "<path d='M3 12h3l3-8 4 16 3-8h5'/>"},
    {WebPageKey::kBackends, "/backends", "Backends",
     "<ellipse cx='12' cy='5.5' rx='7' ry='2.5'/>"
     "<path d='M5 5.5v6c0 1.4 3.1 2.5 7 2.5s7-1.1 7-2.5v-6'/>"
     "<path d='M5 11.5v6c0 1.4 3.1 2.5 7 2.5s7-1.1 7-2.5v-6'/>"},
    {WebPageKey::kDiagnostics, "/diagnostics", "Diagnostics",
     "<path d='M12 3a9 9 0 1 0 9 9'/>"
     "<path d='M12 7v5l3 2'/>"
     "<path d='M17 3l4 4-4 4'/>"},
}};
// clang-format on

struct EmbeddedTemplateView {
    const char* data;
    std::size_t size;
};

extern const std::uint8_t page_root_html_start[] asm("_binary_page_root_html_start");
extern const std::uint8_t page_root_html_end[] asm("_binary_page_root_html_end");
extern const std::uint8_t page_config_html_start[] asm("_binary_page_config_html_start");
extern const std::uint8_t page_config_html_end[] asm("_binary_page_config_html_end");
extern const std::uint8_t page_sensors_html_start[] asm("_binary_page_sensors_html_start");
extern const std::uint8_t page_sensors_html_end[] asm("_binary_page_sensors_html_end");
extern const std::uint8_t page_backends_html_start[] asm("_binary_page_backends_html_start");
extern const std::uint8_t page_backends_html_end[] asm("_binary_page_backends_html_end");
extern const std::uint8_t page_diagnostics_html_start[] asm("_binary_page_diagnostics_html_start");
extern const std::uint8_t page_diagnostics_html_end[] asm("_binary_page_diagnostics_html_end");
extern const std::uint8_t card_backend_html_start[] asm("_binary_card_backend_html_start");
extern const std::uint8_t card_backend_html_end[] asm("_binary_card_backend_html_end");
extern const std::uint8_t card_sensor_html_start[] asm("_binary_card_sensor_html_start");
extern const std::uint8_t card_sensor_html_end[] asm("_binary_card_sensor_html_end");
extern const std::uint8_t item_overview_backend_html_start[] asm("_binary_item_overview_backend_html_start");
extern const std::uint8_t item_overview_backend_html_end[] asm("_binary_item_overview_backend_html_end");
extern const std::uint8_t item_overview_sensor_html_start[] asm("_binary_item_overview_sensor_html_start");
extern const std::uint8_t item_overview_sensor_html_end[] asm("_binary_item_overview_sensor_html_end");

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

EmbeddedTemplateView makeTemplateView(const std::uint8_t* start, const std::uint8_t* end) {
    return EmbeddedTemplateView{
        reinterpret_cast<const char*>(start),
        embeddedTextSize(start, end),
    };
}

std::string firmwareVersionLabel() {
    const esp_app_desc_t* app = esp_app_get_description();
    if (app == nullptr || app->version[0] == '\0') {
        return "version unavailable";
    }

    return std::string("\xc2\xb7 ") + app->version;
}

const char* pageDataKey(WebPageKey page) {
    switch (page) {
        case WebPageKey::kHome:         return "overview";
        case WebPageKey::kConfig:       return "device";
        case WebPageKey::kSensors:      return "sensors";
        case WebPageKey::kBackends:     return "backends";
        case WebPageKey::kDiagnostics:  return "diagnostics";
        default:                        return "overview";
    }
}

const char* pageCrumb(WebPageKey page) {
    switch (page) {
        case WebPageKey::kHome:         return "OVERVIEW";
        case WebPageKey::kConfig:       return "DEVICE";
        case WebPageKey::kSensors:      return "SENSORS";
        case WebPageKey::kBackends:     return "BACKENDS";
        case WebPageKey::kDiagnostics:  return "DIAGNOSTICS";
        default:                        return "";
    }
}

const EmbeddedTemplateView* findEmbeddedTemplate(WebTemplateKey template_key) {
    static const EmbeddedTemplateView kRootTemplate =
        makeTemplateView(page_root_html_start, page_root_html_end);
    static const EmbeddedTemplateView kConfigTemplate =
        makeTemplateView(page_config_html_start, page_config_html_end);
    static const EmbeddedTemplateView kSensorsTemplate =
        makeTemplateView(page_sensors_html_start, page_sensors_html_end);
    static const EmbeddedTemplateView kBackendsTemplate =
        makeTemplateView(page_backends_html_start, page_backends_html_end);
    static const EmbeddedTemplateView kDiagnosticsTemplate =
        makeTemplateView(page_diagnostics_html_start, page_diagnostics_html_end);
    static const EmbeddedTemplateView kBackendCardTemplate =
        makeTemplateView(card_backend_html_start, card_backend_html_end);
    static const EmbeddedTemplateView kSensorCardTemplate =
        makeTemplateView(card_sensor_html_start, card_sensor_html_end);
    static const EmbeddedTemplateView kOverviewBackendItemTemplate =
        makeTemplateView(item_overview_backend_html_start, item_overview_backend_html_end);
    static const EmbeddedTemplateView kOverviewSensorItemTemplate =
        makeTemplateView(item_overview_sensor_html_start, item_overview_sensor_html_end);

    switch (template_key) {
        case WebTemplateKey::kHome:
            return &kRootTemplate;
        case WebTemplateKey::kConfig:
            return &kConfigTemplate;
        case WebTemplateKey::kSensors:
            return &kSensorsTemplate;
        case WebTemplateKey::kBackends:
            return &kBackendsTemplate;
        case WebTemplateKey::kDiagnostics:
            return &kDiagnosticsTemplate;
        case WebTemplateKey::kBackendCard:
            return &kBackendCardTemplate;
        case WebTemplateKey::kSensorCard:
            return &kSensorCardTemplate;
        case WebTemplateKey::kOverviewBackendItem:
            return &kOverviewBackendItemTemplate;
        case WebTemplateKey::kOverviewSensorItem:
            return &kOverviewSensorItemTemplate;
        default:
            return nullptr;
    }
}

void replaceAll(std::string& text, std::string_view needle, std::string_view replacement) {
    if (needle.empty()) {
        return;
    }

    std::size_t position = 0U;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

}  // namespace

std::string htmlEscape(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());

    for (const char ch : input) {
        switch (ch) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

std::string renderNotice(const std::string& notice, bool error_notice) {
    if (notice.empty()) {
        return "";
    }

    std::string html;
    html.reserve(notice.size() + 64U);
    html += "<div class='banner ";
    html += error_notice ? "err" : "ok";
    html += "'>";
    html += htmlEscape(notice);
    html += "</div>";
    return html;
}

std::string renderPageTemplate(
    WebTemplateKey template_key,
    const WebTemplateBindings& bindings) {
    return renderTemplate(template_key, bindings);
}

std::string renderTemplate(
    WebTemplateKey template_key,
    const WebTemplateBindings& bindings) {
    const EmbeddedTemplateView* templ = findEmbeddedTemplate(template_key);
    if (templ == nullptr || templ->data == nullptr || templ->size == 0U) {
        return "";
    }

    std::string rendered(templ->data, templ->size);
    for (const auto& [key, value] : bindings) {
        std::string placeholder;
        placeholder.reserve(key.size() + 4U);
        placeholder += "{{";
        placeholder += key;
        placeholder += "}}";
        replaceAll(rendered, placeholder, value);
    }

    return rendered;
}

std::string renderPageDocument(
    WebPageKey active_page,
    std::string_view title,
    std::string_view heading,
    std::string_view lead_html,
    std::string_view body_html,
    bool /*wide_layout*/,
    bool device_only_navigation) {
    std::string html;
    html.reserve(body_html.size() + 2400U);

    // Document head
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>"; html += htmlEscape(title); html += "</title>";
    html += "<link rel='stylesheet' href='"; html += webUiStylesHref(); html += "'>";
    html += "<script defer src='"; html += webUiScriptHref(); html += "'></script>";
    html += "</head><body data-page='"; html += pageDataKey(active_page); html += "'>";

    // App shell
    html += "<div class='app'>";

    // ── Top navigation bar ──────────────────────────────────────────────────
    html += "<header class='topnav'>";
    html += "<div class='topnav-brand'>";
    html += "<div class='brand-mark'>A</div>";
    html += "<div style='display:flex;flex-direction:column'>";
    html += "<div class='brand-name'>AIR360</div>";
    html += "<div class='brand-sub'>"; html += htmlEscape(firmwareVersionLabel()); html += "</div>";
    html += "</div></div>";
    html += "<nav class='topnav-nav'>";

    static constexpr char kSvgOpen[] =
        "<svg viewBox='0 0 24 24' fill='none' stroke='currentColor'"
        " stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'>";

    for (const auto& item : kNavItems) {
        if (device_only_navigation && item.page != WebPageKey::kConfig) {
            continue;
        }
        html += "<a class='nav-item";
        if (item.page == active_page) {
            html += " active";
        }
        html += "' href='"; html += item.href; html += "'>";
        html += kSvgOpen; html += item.icon; html += "</svg>";
        html += htmlEscape(item.label);
        html += "</a>";
    }

    html += "</nav><div class='topnav-right'>";
    html += "<button class='btn sm ghost' id='theme-toggle' type='button'"
            " aria-label='Toggle theme' title='Toggle theme'>";
    html += kSvgOpen;
    html += "<path d='M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z'/>";
    html += "</svg></button>";
    html += "</div></header>";

    // ── Main area ───────────────────────────────────────────────────────────
    html += "<div class='main'>";

    // Page header (crumb + title)
    html += "<div class='page-header'>";
    html += "<div class='crumb'>"; html += pageCrumb(active_page); html += "</div>";
    html += "<div class='title-row'>";
    html += "<h1 class='page-title'>"; html += htmlEscape(heading); html += "</h1>";
    html += "</div></div>";

    if (!lead_html.empty()) {
        html += "<p class='lead'>"; html += lead_html; html += "</p>";
    }

    // Page content
    html += "<div class='content'>"; html += body_html; html += "</div>";

    html += "</div></div></body></html>";
    return html;
}

}  // namespace air360
