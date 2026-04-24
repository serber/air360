#include "air360/web_request_body.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kTimeoutResult = -3;
constexpr int kDisconnectedResult = -2;

struct ReceiveStep {
    int result = 0;
    std::string data;
};

struct ReceiveScript {
    std::vector<ReceiveStep> steps;
    std::size_t index = 0U;
};

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

int receiveFromScript(void* context, char* buffer, std::size_t buffer_size) {
    auto* script = static_cast<ReceiveScript*>(context);
    if (script->index >= script->steps.size()) {
        return kDisconnectedResult;
    }

    const ReceiveStep& step = script->steps[script->index++];
    if (step.result <= 0) {
        return step.result;
    }

    const std::size_t count =
        std::min<std::size_t>(static_cast<std::size_t>(step.result), buffer_size);
    std::memcpy(buffer, step.data.data(), count);
    return static_cast<int>(count);
}

void testTimeoutsAreRetriedUntilProgress() {
    ReceiveScript script{{
        ReceiveStep{kTimeoutResult, {}},
        ReceiveStep{3, "abc"},
        ReceiveStep{kTimeoutResult, {}},
        ReceiveStep{3, "def"},
    }};
    std::string body;

    const esp_err_t err = air360::web::readRequestBodyWithRetries(
        6U,
        &script,
        receiveFromScript,
        kTimeoutResult,
        body);

    require(err == ESP_OK, "timeouts before progress are retryable");
    requireEqual(body, "abcdef", "body is assembled across retries");
}

void testTooManyConsecutiveTimeoutsReturnTimeout() {
    ReceiveScript script;
    for (int index = 0; index <= air360::web::kMaxRequestBodyReadTimeouts; ++index) {
        script.steps.push_back(ReceiveStep{kTimeoutResult, {}});
    }
    std::string body;

    const esp_err_t err = air360::web::readRequestBodyWithRetries(
        1U,
        &script,
        receiveFromScript,
        kTimeoutResult,
        body);

    require(err == ESP_ERR_TIMEOUT, "consecutive timeouts are bounded");
    require(body.empty(), "partial body is cleared on timeout");
}

void testDisconnectedClientFailsImmediately() {
    ReceiveScript script{{
        ReceiveStep{kDisconnectedResult, {}},
    }};
    std::string body;

    const esp_err_t err = air360::web::readRequestBodyWithRetries(
        4U,
        &script,
        receiveFromScript,
        kTimeoutResult,
        body);

    require(err == ESP_FAIL, "non-timeout receive errors fail");
    require(body.empty(), "body is cleared on receive error");
}

void testOversizedBodyIsRejectedBeforeReceive() {
    ReceiveScript script;
    std::string body;

    const esp_err_t err = air360::web::readRequestBodyWithRetries(
        air360::web::kHttpMaxRequestBodySize + 1U,
        &script,
        receiveFromScript,
        kTimeoutResult,
        body);

    require(err == ESP_ERR_INVALID_SIZE, "oversized body is rejected");
    require(script.index == 0U, "oversized body does not call receiver");
}

}  // namespace

int main() {
    testTimeoutsAreRetriedUntilProgress();
    testTooManyConsecutiveTimeoutsReturnTimeout();
    testDisconnectedClientFailsImmediately();
    testOversizedBodyIsRejectedBeforeReceive();
    std::cout << "web_request_body tests passed\n";
    return 0;
}
