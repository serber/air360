#pragma once

#include <cstddef>
#include <cstdint>

namespace air360 {

constexpr std::size_t kMaxConfiguredBackends = 4U;
constexpr std::size_t kBackendDisplayNameCapacity = 32U;
constexpr std::size_t kBackendUrlCapacity = 160U;
constexpr std::size_t kBackendTokenCapacity = 160U;

enum class BackendType : std::uint8_t {
    kUnknown = 0U,
    kSensorCommunity = 1U,
    kAir360Api = 2U,
};

enum class BackendRuntimeState : std::uint8_t {
    kDisabled = 0U,
    kIdle = 1U,
    kUploading = 2U,
    kOk = 3U,
    kError = 4U,
    kNotImplemented = 5U,
};

enum class UploadResultClass : std::uint8_t {
    kUnknown = 0U,
    kSuccess = 1U,
    kNoData = 2U,
    kNoNetwork = 3U,
    kTransportError = 4U,
    kHttpError = 5U,
    kConfigError = 6U,
    kUnsupported = 7U,
};

inline const char* backendTypeKey(BackendType type) {
    switch (type) {
        case BackendType::kSensorCommunity:
            return "sensor_community";
        case BackendType::kAir360Api:
            return "air360_api";
        case BackendType::kUnknown:
        default:
            return "unknown";
    }
}

inline const char* backendRuntimeStateKey(BackendRuntimeState state) {
    switch (state) {
        case BackendRuntimeState::kDisabled:
            return "disabled";
        case BackendRuntimeState::kIdle:
            return "idle";
        case BackendRuntimeState::kUploading:
            return "uploading";
        case BackendRuntimeState::kOk:
            return "ok";
        case BackendRuntimeState::kError:
            return "error";
        case BackendRuntimeState::kNotImplemented:
            return "not_implemented";
        default:
            return "unknown";
    }
}

inline const char* uploadResultClassKey(UploadResultClass result) {
    switch (result) {
        case UploadResultClass::kSuccess:
            return "success";
        case UploadResultClass::kNoData:
            return "no_data";
        case UploadResultClass::kNoNetwork:
            return "no_network";
        case UploadResultClass::kTransportError:
            return "transport_error";
        case UploadResultClass::kHttpError:
            return "http_error";
        case UploadResultClass::kConfigError:
            return "config_error";
        case UploadResultClass::kUnsupported:
            return "unsupported";
        case UploadResultClass::kUnknown:
        default:
            return "unknown";
    }
}

}  // namespace air360
