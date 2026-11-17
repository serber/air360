#pragma once

#include <string>
#include <utility>
#include <vector>

namespace air360::web {

using FormFields = std::vector<std::pair<std::string, std::string>>;

std::string urlDecode(const std::string& input);
FormFields parseFormBody(const std::string& body);
std::string findFormValue(const FormFields& fields, const char* key);
bool formHasKey(const FormFields& fields, const char* key);
// Parses a complete decimal float. Rejects empty input, trailing characters,
// out-of-range values (e.g. 1e999) and non-finite results (nan, inf).
bool parseFiniteFloat(const std::string& input, float& value);

}  // namespace air360::web
