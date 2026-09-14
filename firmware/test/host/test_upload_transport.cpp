#include "air360/uploads/upload_transport.hpp"
#include "esp_http_client.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    air360::UploadRequestSpec request;
    request.url = "https://example.test/measurements";
    request.body = "{}";
    request.headers.push_back({"Retry-After", "12"});
    http_test::response_headers = {{"Content-Type", "text/plain"}, {"rEtRy-AfTeR", " 120\t"}};
    http_test::body = "slow down\nplease";
    auto response = air360::UploadTransport{}.execute(request);
    require(response.http_status == 429 && response.retry_after_seconds == 120U,
            "server response header takes precedence over outgoing header");
    require(response.body_snippet == "slow down please", "body capture still works");
    require(http_test::cleaned_up == 1U, "HTTP client is cleaned up");
    http_test::status = 503;
    http_test::response_headers = {{"retry-after", "3600"}};
    require(air360::UploadTransport{}.execute(request).retry_after_seconds == 3600U,
            "maximum supported server delay is accepted");
    for (const char* invalid : {"", " ", "0", "3601", "-1", "+12", "12x", "1 2",
                                "999999999999999999999", "Wed, 21 Oct 2015 07:28:00 GMT"}) {
        http_test::response_headers = {{"Retry-After", invalid}};
        require(air360::UploadTransport{}.execute(request).retry_after_seconds == 0U,
                "invalid or unsupported delay is ignored");
    }
    http_test::response_headers.clear();
    require(air360::UploadTransport{}.execute(request).retry_after_seconds == 0U,
            "absent response header does not use outgoing header or previous response");
    http_test::response_headers = {{"Retry-After", "60"}};
    http_test::result = ESP_ERR_TIMEOUT;
    response = air360::UploadTransport{}.execute(request);
    require(response.transport_err == ESP_ERR_TIMEOUT && response.retry_after_seconds == 0U,
            "failed transport does not publish partial response metadata");
    std::cout << "upload transport tests passed\n";
}
