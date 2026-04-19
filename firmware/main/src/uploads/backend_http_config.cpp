#include "air360/uploads/backend_http_config.hpp"

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace air360 {

namespace {

constexpr std::string_view kHttpScheme = "http://";
constexpr std::string_view kHttpsScheme = "https://";

bool isNullTerminated(const char* value, std::size_t capacity) {
    if (value == nullptr || capacity == 0U) {
        return false;
    }
    return value[capacity - 1U] == '\0';
}

std::string boundedCString(const char* value, std::size_t capacity) {
    if (value == nullptr || capacity == 0U) {
        return "";
    }

    std::size_t length = 0U;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }

    return std::string(value, length);
}

bool containsLineBreak(std::string_view value) {
    return value.find('\n') != std::string_view::npos ||
           value.find('\r') != std::string_view::npos;
}

void copyString(char* destination, std::size_t destination_size, std::string_view source) {
    if (destination_size == 0U) {
        return;
    }

    const std::size_t count =
        source.size() < (destination_size - 1U) ? source.size() : (destination_size - 1U);
    if (count > 0U) {
        std::memcpy(destination, source.data(), count);
    }
    destination[count] = '\0';
    for (std::size_t index = count + 1U; index < destination_size; ++index) {
        destination[index] = '\0';
    }
}

bool validateBackendTextFields(const BackendHttpConfigView& config, std::string& error) {
    if (containsLineBreak(config.host) ||
        containsLineBreak(config.path) ||
        containsLineBreak(config.username) ||
        containsLineBreak(config.password) ||
        containsLineBreak(config.measurement_name)) {
        error = "Backend fields must not contain line breaks.";
        return false;
    }

    if (!config.host.empty() &&
        (config.host.rfind(kHttpScheme, 0U) == 0U || config.host.rfind(kHttpsScheme, 0U) == 0U)) {
        error = "Backend host must not include http:// or https://.";
        return false;
    }

    if (!config.path.empty() && config.path.front() != '/') {
        error = "Backend path must start with '/'.";
        return false;
    }

    error.clear();
    return true;
}

bool validateBackendEndpoint(const BackendHttpConfigView& config, std::string& error) {
    if (config.host.empty()) {
        error = "Backend host must not be empty.";
        return false;
    }

    if (config.path.empty()) {
        error = "Backend path must not be empty.";
        return false;
    }

    if (config.port == 0U) {
        error = "Backend port must be greater than zero.";
        return false;
    }

    error.clear();
    return true;
}

bool isDefaultPort(const BackendHttpConfigView& config) {
    return (config.use_https && config.port == 443U) || (!config.use_https && config.port == 80U);
}

bool validateBackendTypeRules(
    const BackendHttpConfigView& config,
    BackendType type,
    std::string& error) {
    switch (type) {
        case BackendType::kInfluxDb:
            if (config.measurement_name.empty()) {
                error = "Measurement name must not be empty.";
                return false;
            }
            error.clear();
            return true;
        case BackendType::kSensorCommunity:
        case BackendType::kAir360Api:
        case BackendType::kCustomUpload:
        case BackendType::kUnknown:
        default:
            error.clear();
            return true;
    }
}

bool validateBackendHttpConfigForRequest(
    const BackendHttpConfigView& config,
    std::string& error) {
    if (!validateBackendTextFields(config, error)) {
        return false;
    }
    return validateBackendEndpoint(config, error);
}

bool validateBackendHttpConfigForRecord(
    const BackendHttpConfigView& config,
    const BackendRecord& record,
    std::string& error) {
    if (record.enabled == 0U) {
        error.clear();
        return true;
    }

    if (!validateBackendTextFields(config, error)) {
        return false;
    }

    if (!validateBackendEndpoint(config, error)) {
        return false;
    }

    return validateBackendTypeRules(config, record.backend_type, error);
}

}  // namespace

BackendHttpConfigView defaultBackendHttpConfig(BackendType type) {
    BackendHttpConfigView config;
    config.use_https = backendDefaultUseHttps(type);
    config.host = backendDefaultHost(type);
    config.path = backendDefaultPath(type);
    config.port = backendDefaultPort(type);
    if (type == BackendType::kInfluxDb) {
        config.measurement_name = "air360";
    }
    if (config.port == 0U) {
        config.port = static_cast<std::uint16_t>(config.use_https ? 443U : 80U);
    }
    return config;
}

bool buildBackendHttpUrl(
    const BackendHttpConfigView& config,
    std::string& out_url,
    std::string& error) {
    out_url.clear();
    if (!validateBackendHttpConfigForRequest(config, error)) {
        return false;
    }

    out_url.assign(config.use_https ? kHttpsScheme : kHttpScheme);
    out_url += config.host;
    out_url += ':';
    out_url += std::to_string(config.port);
    out_url += config.path;
    error.clear();
    return true;
}

std::string formatBackendHttpDisplayEndpoint(const BackendHttpConfigView& config) {
    if (config.host.empty()) {
        return "";
    }

    std::string endpoint = config.host;
    if (!isDefaultPort(config)) {
        endpoint += ':';
        endpoint += std::to_string(config.port);
    }
    endpoint += config.path;
    return endpoint;
}

bool decodeBackendHttpRecord(
    const BackendRecord& record,
    BackendHttpConfigView& out_config,
    std::string& error) {
    out_config = BackendHttpConfigView{};

    if (!isNullTerminated(record.host, kBackendHostCapacity) ||
        !isNullTerminated(record.path, kBackendPathCapacity) ||
        !isNullTerminated(record.username, kBackendUsernameCapacity) ||
        !isNullTerminated(record.password, kBackendPasswordCapacity) ||
        !isNullTerminated(record.measurement_name, kBackendMeasurementCapacity)) {
        error = "Backend config is not null-terminated.";
        return false;
    }

    out_config.use_https = record.use_https != 0U;
    out_config.host = boundedCString(record.host, sizeof(record.host));
    out_config.path = boundedCString(record.path, sizeof(record.path));
    out_config.port =
        record.port != 0U ? record.port : static_cast<std::uint16_t>(out_config.use_https ? 443U : 80U);
    out_config.username = boundedCString(record.username, sizeof(record.username));
    out_config.password = boundedCString(record.password, sizeof(record.password));
    out_config.measurement_name =
        boundedCString(record.measurement_name, sizeof(record.measurement_name));

    error.clear();
    return true;
}

bool validateBackendHttpRecord(const BackendRecord& record, std::string& error) {
    BackendHttpConfigView config;
    if (!decodeBackendHttpRecord(record, config, error)) {
        return false;
    }

    return validateBackendHttpConfigForRecord(config, record, error);
}

bool encodeBackendHttpRecord(
    const BackendHttpConfigView& config,
    BackendRecord& record,
    std::string& error) {
    if (config.host.size() >= kBackendHostCapacity) {
        error = "Backend host is too long.";
        return false;
    }
    if (config.path.size() >= kBackendPathCapacity) {
        error = "Backend path is too long.";
        return false;
    }
    if (config.username.size() >= kBackendUsernameCapacity) {
        error = "Backend username is too long.";
        return false;
    }
    if (config.password.size() >= kBackendPasswordCapacity) {
        error = "Backend password is too long.";
        return false;
    }
    if (config.measurement_name.size() >= kBackendMeasurementCapacity) {
        error = "Measurement name is too long.";
        return false;
    }

    BackendHttpConfigView normalized = config;
    if (normalized.port == 0U) {
        normalized.port = static_cast<std::uint16_t>(normalized.use_https ? 443U : 80U);
    }
    if (!validateBackendHttpConfigForRecord(normalized, record, error)) {
        return false;
    }

    copyString(record.host, sizeof(record.host), normalized.host);
    copyString(record.path, sizeof(record.path), normalized.path);
    copyString(record.username, sizeof(record.username), normalized.username);
    copyString(record.password, sizeof(record.password), normalized.password);
    copyString(
        record.measurement_name,
        sizeof(record.measurement_name),
        normalized.measurement_name);
    record.port = normalized.port;
    record.use_https = normalized.use_https ? 1U : 0U;

    error.clear();
    return true;
}

}  // namespace air360
