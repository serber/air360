#include "air360/web_ui.hpp"

#include <array>
#include <cstdint>

#include "air360/web_assets.hpp"

namespace air360 {

namespace {

struct NavItem {
    WebPageKey page;
    const char* href;
    const char* label;
};

constexpr std::array<NavItem, 4> kNavItems{{
    {WebPageKey::kHome, "/", "Overview"},
    {WebPageKey::kConfig, "/config", "Device"},
    {WebPageKey::kSensors, "/sensors", "Sensors"},
    {WebPageKey::kBackends, "/backends", "Backends"},
}};

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

const EmbeddedTemplateView* findEmbeddedTemplate(WebTemplateKey template_key) {
    static const EmbeddedTemplateView kRootTemplate =
        makeTemplateView(page_root_html_start, page_root_html_end);
    static const EmbeddedTemplateView kConfigTemplate =
        makeTemplateView(page_config_html_start, page_config_html_end);
    static const EmbeddedTemplateView kSensorsTemplate =
        makeTemplateView(page_sensors_html_start, page_sensors_html_end);
    static const EmbeddedTemplateView kBackendsTemplate =
        makeTemplateView(page_backends_html_start, page_backends_html_end);
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
    html += "<div class='notice ";
    html += error_notice ? "notice--err" : "notice--ok";
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
    bool wide_layout) {
    std::string html;
    html.reserve(body_html.size() + 1200U);

    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>";
    html += htmlEscape(title);
    html += "</title>";
    html += "<link rel='stylesheet' href='";
    html += webUiStylesHref();
    html += "'>";
    html += "<script defer src='";
    html += webUiScriptHref();
    html += "'></script>";
    html += "</head><body><div class='shell";
    if (!wide_layout) {
        html += " shell--narrow";
    }
    html += "'><div class='chrome'><header class='topbar'>";
    html += "<div class='brand'><div class='brand__eyebrow'>Air360 Firmware</div>";
    html += "<div class='brand__title'>";
    html += htmlEscape(heading);
    html += "</div></div><nav class='nav'>";

    for (const auto& item : kNavItems) {
        html += "<a class='nav__link";
        if (item.page == active_page) {
            html += " nav__link--active";
        }
        html += "' href='";
        html += item.href;
        html += "'>";
        html += htmlEscape(item.label);
        html += "</a>";
    }

    html += "<a class='nav__link nav__link--muted' href='/status'>Status JSON</a>";
    html += "</nav></header><main class='page'><section class='pagehead'>";
    html += "<div class='eyebrow'>Local Runtime</div><h1>";
    html += htmlEscape(heading);
    html += "</h1>";
    if (!lead_html.empty()) {
        html += "<p class='lead'>";
        html += lead_html;
        html += "</p>";
    }
    html += "</section>";
    html += body_html;
    html += "</main></div></div></body></html>";
    return html;
}

}  // namespace air360
