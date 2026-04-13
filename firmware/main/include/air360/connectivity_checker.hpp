#pragma once

#include <cstdint>

namespace air360 {

enum class ConnectivityCheckResult : std::uint8_t {
    kSkipped,  // host is null or empty
    kOk,       // at least one ICMP echo reply received
    kFailed,   // no reply within timeout × retries
};

// Performs a synchronous ICMP connectivity check.
//   host       — IPv4 address string (e.g. "8.8.8.8"); null or empty = skip
//   timeout_ms — per-attempt deadline in milliseconds
//   retries    — number of echo requests to send
// Blocks the calling task until all attempts are exhausted or the first
// reply arrives.  Safe to call from any FreeRTOS task context.
ConnectivityCheckResult runConnectivityCheck(
    const char* host,
    std::uint32_t timeout_ms,
    std::uint32_t retries);

}  // namespace air360
