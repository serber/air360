#include "air360/uploads/upload_log_endpoint.hpp"

#include <cctype>
#include <cstdint>

namespace air360 {
namespace {

constexpr std::string_view kInvalidEndpoint = "<invalid-url>";

std::string toLowerAscii(std::string_view value) {
    std::string lower;
    lower.reserve(value.size());
    for (const char ch : value) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower;
}

bool isValidScheme(std::string_view scheme) {
    if (scheme.empty() || !std::isalpha(static_cast<unsigned char>(scheme.front()))) {
        return false;
    }
    for (const char ch : scheme) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '+' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

bool parsePort(std::string_view text, std::uint32_t& port) {
    if (text.empty()) {
        return false;
    }
    std::uint32_t value = 0U;
    for (const char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint32_t>(ch - '0');
        if (value > 65535U) {
            return false;
        }
    }
    port = value;
    return true;
}

bool isDefaultPort(std::string_view scheme, std::uint32_t port) {
    return (scheme == "http" && port == 80U) || (scheme == "https" && port == 443U);
}

bool containsWhitespace(std::string_view value) {
    for (const char ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string formatUploadEndpointForLog(std::string_view url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string(kInvalidEndpoint);
    }

    const std::string scheme = toLowerAscii(url.substr(0U, scheme_end));
    if (!isValidScheme(scheme)) {
        return std::string(kInvalidEndpoint);
    }

    const std::size_t authority_start = scheme_end + 3U;
    const std::size_t authority_end = url.find_first_of("/?#", authority_start);
    std::string_view authority = url.substr(
        authority_start,
        authority_end == std::string_view::npos ? std::string_view::npos
                                                : authority_end - authority_start);
    if (authority.empty() || containsWhitespace(authority)) {
        return std::string(kInvalidEndpoint);
    }

    const std::size_t userinfo_end = authority.rfind('@');
    if (userinfo_end != std::string_view::npos) {
        authority.remove_prefix(userinfo_end + 1U);
        if (authority.empty()) {
            return std::string(kInvalidEndpoint);
        }
    }

    std::string_view host = authority;
    std::string_view port_text;

    if (authority.front() == '[') {
        const std::size_t bracket_end = authority.find(']');
        if (bracket_end == std::string_view::npos) {
            return std::string(kInvalidEndpoint);
        }
        host = authority.substr(0U, bracket_end + 1U);
        if (bracket_end + 1U < authority.size()) {
            if (authority[bracket_end + 1U] != ':') {
                return std::string(kInvalidEndpoint);
            }
            port_text = authority.substr(bracket_end + 2U);
        }
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos && authority.find(':') == colon) {
            host = authority.substr(0U, colon);
            port_text = authority.substr(colon + 1U);
        } else if (colon != std::string_view::npos) {
            return std::string(kInvalidEndpoint);
        }
    }

    if (host.empty() || containsWhitespace(host)) {
        return std::string(kInvalidEndpoint);
    }

    std::string endpoint;
    endpoint.reserve(scheme.size() + 3U + host.size() + 6U);
    endpoint += scheme;
    endpoint += "://";
    endpoint.append(host.data(), host.size());

    if (!port_text.empty()) {
        std::uint32_t port = 0U;
        if (!parsePort(port_text, port)) {
            endpoint += ":<invalid-port>";
        } else if (!isDefaultPort(scheme, port)) {
            endpoint += ':';
            endpoint += std::to_string(port);
        }
    }

    return endpoint;
}

}  // namespace air360
