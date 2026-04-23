#include "air360/web_form.hpp"

#include <cstddef>

namespace air360::web {

namespace {

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
            // Only key presence matters for checkbox-style form fields.
            static_cast<void>(value);
            return true;
        }
    }
    return false;
}

}  // namespace air360::web
