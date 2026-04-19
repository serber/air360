#pragma once

#include <cstdint>
#include <string>

#include "air360/uploads/backend_config.hpp"

namespace air360 {

struct BackendHttpConfigView {
    bool use_https = true;
    std::string host;
    std::string path;
    std::uint16_t port = 443U;
    std::string username;
    std::string password;
    std::string measurement_name;
};

BackendHttpConfigView defaultBackendHttpConfig(BackendType type);

bool decodeBackendHttpRecord(
    const BackendRecord& record,
    BackendHttpConfigView& out_config,
    std::string& error);

bool buildBackendHttpUrl(
    const BackendHttpConfigView& config,
    std::string& out_url,
    std::string& error);

std::string formatBackendHttpDisplayEndpoint(const BackendHttpConfigView& config);

bool validateBackendHttpRecord(const BackendRecord& record, std::string& error);

bool encodeBackendHttpRecord(
    const BackendHttpConfigView& config,
    BackendRecord& record,
    std::string& error);

}  // namespace air360
