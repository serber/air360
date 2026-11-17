#include "air360/string_utils.hpp"
#include "air360/uploads/backend_config.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
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

void testAir360LocationInvariant() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    require(air360::isValidAir360Location(55.75F, 37.62F, 150.0F), "ordinary location valid");
    require(air360::isValidAir360Location(-90.0F, -180.0F, 0.0F), "lower bounds valid");
    require(air360::isValidAir360Location(90.0F, 180.0F, -10.0F), "upper bounds valid");
    require(air360::isValidAir360Location(0.0F, 0.0F, 0.0F), "unset location is well-formed");
    require(!air360::isValidAir360Location(nan, 0.0F, 0.0F), "nan latitude rejected");
    require(!air360::isValidAir360Location(0.0F, inf, 0.0F), "inf longitude rejected");
    require(!air360::isValidAir360Location(0.0F, 0.0F, -inf), "-inf altitude rejected");
    require(!air360::isValidAir360Location(0.0F, 0.0F, nan), "nan altitude rejected");
    require(!air360::isValidAir360Location(90.5F, 0.0F, 0.0F), "latitude above 90 rejected");
    require(!air360::isValidAir360Location(0.0F, -180.5F, 0.0F), "longitude below -180 rejected");
}

int main() {
    testAir360LocationInvariant();
    testDefaultPortsAreOmittedFromUrls();
    testCustomPortsStayInUrls();
    std::cout << "backend_config tests passed\n";
    return 0;
}
