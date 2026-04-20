#include "air360/web_server_internal.hpp"

#include <cstddef>

namespace air360::web {

namespace {

constexpr std::size_t kHttpMaxRequestBodySize = 4096U;

int decodeHex(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::string urlDecode(const std::string& input) {
    std::string decoded;
    decoded.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (ch == '%' && (i + 2U) < input.size()) {
            const int hi = decodeHex(input[i + 1U]);
            const int lo = decodeHex(input[i + 2U]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2U;
                continue;
            }
        }

        decoded.push_back(ch);
    }

    return decoded;
}

FormFields parseFormBody(const std::string& body) {
    FormFields fields;
    std::size_t cursor = 0U;

    while (cursor <= body.size()) {
        const std::size_t delimiter = body.find('&', cursor);
        const std::size_t end = delimiter == std::string::npos ? body.size() : delimiter;
        const std::size_t equals = body.find('=', cursor);

        if (equals != std::string::npos && equals < end) {
            fields.emplace_back(
                urlDecode(body.substr(cursor, equals - cursor)),
                urlDecode(body.substr(equals + 1U, end - equals - 1U)));
        } else if (end > cursor) {
            fields.emplace_back(urlDecode(body.substr(cursor, end - cursor)), "");
        }

        if (delimiter == std::string::npos) {
            break;
        }
        cursor = delimiter + 1U;
    }

    return fields;
}

std::string findFormValue(const FormFields& fields, const char* key) {
    for (const auto& [name, value] : fields) {
        if (name == key) {
            return value;
        }
    }
    return "";
}

bool formHasKey(const FormFields& fields, const char* key) {
    for (const auto& [name, value] : fields) {
        if (name == key) {
            static_cast<void>(value);
            return true;
        }
    }
    return false;
}

esp_err_t readRequestBody(httpd_req_t* request, std::string& out_body) {
    out_body.clear();
    if (request->content_len <= 0) {
        return ESP_OK;
    }
    if (request->content_len > static_cast<int>(kHttpMaxRequestBodySize)) {
        return ESP_ERR_INVALID_SIZE;
    }

    out_body.resize(static_cast<std::size_t>(request->content_len));
    int received_total = 0;
    while (received_total < request->content_len) {
        const int received = httpd_req_recv(
            request,
            out_body.data() + received_total,
            request->content_len - received_total);
        if (received <= 0) {
            return ESP_FAIL;
        }
        received_total += received;
    }

    return ESP_OK;
}

esp_err_t sendRequestBodyTooLarge(httpd_req_t* request) {
    httpd_resp_set_status(request, "413 Payload Too Large");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Request body exceeds the 4096-byte limit.");
}

esp_err_t sendHtmlResponse(httpd_req_t* request, const std::string& html) {
    constexpr std::size_t kChunkSize = 1024U;

    for (std::size_t offset = 0; offset < html.size(); offset += kChunkSize) {
        const std::size_t remaining = html.size() - offset;
        const std::size_t chunk_size = remaining < kChunkSize ? remaining : kChunkSize;
        const esp_err_t err =
            httpd_resp_send_chunk(request, html.data() + offset, chunk_size);
        if (err != ESP_OK) {
            return err;
        }
    }

    return httpd_resp_send_chunk(request, nullptr, 0);
}

}  // namespace air360::web
