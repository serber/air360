#include "air360/web_form.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void requireEqual(const std::string& actual, const std::string& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        std::exit(1);
    }
}

void testUrlDecode() {
    requireEqual(air360::web::urlDecode("hello+world"), "hello world", "plus decodes to space");
    requireEqual(air360::web::urlDecode("ssid=Lab%20WiFi"), "ssid=Lab WiFi", "hex escape decodes");
    requireEqual(air360::web::urlDecode("%2fconfig%3fa%3d1"), "/config?a=1", "lowercase hex decodes");
    requireEqual(air360::web::urlDecode("bad%zztail"), "bad%zztail", "bad escape is preserved");
    requireEqual(air360::web::urlDecode("trailing%"), "trailing%", "trailing percent is preserved");
}

void testParseFormBody() {
    const air360::web::FormFields fields =
        air360::web::parseFormBody("device_name=Air+360&wifi_ssid=Lab%20WiFi&enabled");

    require(fields.size() == 3U, "three form fields parsed");
    requireEqual(air360::web::findFormValue(fields, "device_name"), "Air 360", "device name parsed");
    requireEqual(air360::web::findFormValue(fields, "wifi_ssid"), "Lab WiFi", "ssid parsed");
    requireEqual(air360::web::findFormValue(fields, "enabled"), "", "key without value parsed");
    require(air360::web::formHasKey(fields, "enabled"), "key without value is present");
    require(!air360::web::formHasKey(fields, "missing"), "missing key is absent");
}

void testDuplicateAndEmptyFields() {
    const air360::web::FormFields fields =
        air360::web::parseFormBody("a=first&&empty=&a=second&standalone");

    require(fields.size() == 4U, "empty pair between delimiters is ignored");
    requireEqual(air360::web::findFormValue(fields, "a"), "first", "first duplicate wins");
    requireEqual(air360::web::findFormValue(fields, "empty"), "", "explicit empty value parsed");
    require(air360::web::formHasKey(fields, "standalone"), "standalone key is present");
}

}  // namespace

int main() {
    testUrlDecode();
    testParseFormBody();
    testDuplicateAndEmptyFields();
    std::cout << "web_form tests passed\n";
    return 0;
}
