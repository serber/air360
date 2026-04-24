#include "air360/uploads/upload_log_endpoint.hpp"

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

void testPathAndQueryAreOmitted() {
    requireEqual(
        air360::formatUploadEndpointForLog("https://api.example.test/a/b?token=secret"),
        "https://api.example.test",
        "path and query are omitted");
    requireEqual(
        air360::formatUploadEndpointForLog("http://api.example.test/path#fragment"),
        "http://api.example.test",
        "path and fragment are omitted");
}

void testPortsAreLimitedToNonDefaultValues() {
    requireEqual(
        air360::formatUploadEndpointForLog("https://api.example.test:443/a/b"),
        "https://api.example.test",
        "HTTPS default port is omitted");
    requireEqual(
        air360::formatUploadEndpointForLog("http://api.example.test:80/a/b"),
        "http://api.example.test",
        "HTTP default port is omitted");
    requireEqual(
        air360::formatUploadEndpointForLog("https://api.example.test:8443/a/b"),
        "https://api.example.test:8443",
        "HTTPS non-default port is included");
    requireEqual(
        air360::formatUploadEndpointForLog("http://api.example.test:8080/a/b"),
        "http://api.example.test:8080",
        "HTTP non-default port is included");
}

void testCredentialsAndMalformedUrlsDoNotLeakPathData() {
    requireEqual(
        air360::formatUploadEndpointForLog("https://user:secret@api.example.test/a/b"),
        "https://api.example.test",
        "userinfo is omitted");
    requireEqual(
        air360::formatUploadEndpointForLog("https://api.example.test:secret/a/b"),
        "https://api.example.test:<invalid-port>",
        "invalid port value is not copied");
    requireEqual(
        air360::formatUploadEndpointForLog("https://api.example.test:secret:8443/a/b"),
        "<invalid-url>",
        "ambiguous authority is rejected");
    requireEqual(
        air360::formatUploadEndpointForLog("/a/b?token=secret"),
        "<invalid-url>",
        "missing scheme is rejected");
}

}  // namespace

int main() {
    testPathAndQueryAreOmitted();
    testPortsAreLimitedToNonDefaultValues();
    testCredentialsAndMalformedUrlsDoNotLeakPathData();
    std::cout << "upload_log_endpoint tests passed\n";
    return 0;
}
