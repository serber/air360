#include "air360/string_utils.hpp"
#include "air360/uploads/backend_config.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void requireEqual(const std::string& actual, const std::string& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        std::exit(1);
    }
}

air360::BackendRecord makeRecord(
    air360::BackendProtocol protocol,
    std::uint16_t port) {
    air360::BackendRecord record{};
    record.protocol = protocol;
    record.port = port;
    air360::copyString(record.host, sizeof(record.host), "api.example.test");
    air360::copyString(record.path, sizeof(record.path), "/v1/upload");
    return record;
}

void testDefaultPortsAreOmittedFromUrls() {
    requireEqual(
        air360::buildBackendUrl(makeRecord(air360::BackendProtocol::kHttps, 443U)),
        "https://api.example.test/v1/upload",
        "HTTPS default port is omitted");
    requireEqual(
        air360::buildBackendUrl(makeRecord(air360::BackendProtocol::kHttp, 80U)),
        "http://api.example.test/v1/upload",
        "HTTP default port is omitted");
}

void testCustomPortsStayInUrls() {
    requireEqual(
        air360::buildBackendUrl(makeRecord(air360::BackendProtocol::kHttps, 8443U)),
        "https://api.example.test:8443/v1/upload",
        "HTTPS custom port is included");
    requireEqual(
        air360::buildBackendUrl(makeRecord(air360::BackendProtocol::kHttp, 8080U)),
        "http://api.example.test:8080/v1/upload",
        "HTTP custom port is included");
}

}  // namespace

int main() {
    testDefaultPortsAreOmittedFromUrls();
    testCustomPortsStayInUrls();
    std::cout << "backend_config tests passed\n";
    return 0;
}
