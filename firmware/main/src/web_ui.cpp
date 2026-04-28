#include "air360/web_ui.hpp"

#include <cstdint>

#include "air360/web_assets.hpp"
#include "esp_app_desc.h"

namespace air360 {

namespace {

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
extern const std::uint8_t page_shell_html_start[] asm("_binary_page_shell_html_start");
extern const std::uint8_t page_shell_html_end[] asm("_binary_page_shell_html_end");
extern const std::uint8_t item_reading_html_start[] asm("_binary_item_reading_html_start");
extern const std::uint8_t item_reading_html_end[] asm("_binary_item_reading_html_end");
extern const std::uint8_t item_section_row_html_start[] asm("_binary_item_section_row_html_start");
extern const std::uint8_t item_section_row_html_end[] asm("_binary_item_section_row_html_end");

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

    return std::string(app->version);
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
    static const EmbeddedTemplateView kShellTemplate =
        makeTemplateView(page_shell_html_start, page_shell_html_end);
    static const EmbeddedTemplateView kReadingTemplate =
        makeTemplateView(item_reading_html_start, item_reading_html_end);
    static const EmbeddedTemplateView kSectionRowTemplate =
        makeTemplateView(item_section_row_html_start, item_section_row_html_end);
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
        case WebTemplateKey::kShell:
            return &kShellTemplate;
        case WebTemplateKey::kReading:
            return &kReadingTemplate;
        case WebTemplateKey::kSectionRow:
            return &kSectionRowTemplate;
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
    const bool needs_map_assets = active_page == WebPageKey::kBackends;
    const std::string extra_styles =
        needs_map_assets ? "<link rel='stylesheet' href='" + webUiLeafletStylesHref() + "'>" : "";
    const std::string extra_scripts =
        needs_map_assets ? "<script defer src='" + webUiLeafletScriptHref() + "'></script>" : "";
    return renderTemplate(WebTemplateKey::kShell, {
        {"TITLE",            htmlEscape(title)},
        {"STYLES_HREF",      webUiStylesHref()},
        {"SCRIPT_HREF",      webUiScriptHref()},
        {"EXTRA_STYLES",     extra_styles},
        {"EXTRA_SCRIPTS",    extra_scripts},
        {"DATA_PAGE",        pageDataKey(active_page)},
        {"DEVICE_NAV_ATTR",  device_only_navigation ? " data-nav='device-only'" : ""},
        {"FW_VERSION",       htmlEscape(firmwareVersionLabel())},
        {"CRUMB",            pageCrumb(active_page)},
        {"HEADING",          htmlEscape(heading)},
        {"LEAD_HTML",        std::string(lead_html)},
        {"BODY_HTML",        std::string(body_html)},
    });
}

}  // namespace air360
