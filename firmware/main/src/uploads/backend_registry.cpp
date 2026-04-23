#include "air360/uploads/backend_registry.hpp"

#include <cstring>
#include <string>

#include "air360/string_utils.hpp"
#include "air360/uploads/adapters/air360_api_uploader.hpp"
#include "air360/uploads/adapters/custom_upload_uploader.hpp"
#include "air360/uploads/adapters/influxdb_uploader.hpp"
#include "air360/uploads/adapters/sensor_community_uploader.hpp"

namespace air360 {

namespace {

bool validateCommonRecord(const BackendRecord& record, std::string& error) {
    if (record.id == 0U) {
        error = "Backend id must not be zero.";
        return false;
    }

    if (!isNullTerminated(record.display_name, kBackendDisplayNameCapacity) ||
        record.display_name[0] == '\0') {
        error = "Backend display name is invalid.";
        return false;
    }

    if (!isNullTerminated(record.host, kBackendHostCapacity) ||
        !isNullTerminated(record.path, kBackendPathCapacity)) {
        error = "Backend host or path is not null-terminated.";
        return false;
    }

    return true;
}

bool validateHttpEndpoint(const BackendRecord& record, std::string& error) {
    if (record.host[0] == '\0') {
        error = "Backend host must not be empty.";
        return false;
    }

    if (record.path[0] == '\0' || record.path[0] != '/') {
        error = "Backend path must start with '/'.";
        return false;
    }

    if (record.port == 0U) {
        error = "Backend port must be greater than zero.";
        return false;
    }

    if (record.protocol == BackendProtocol::kUnknown) {
        error = "Backend protocol must be set.";
        return false;
    }

    return true;
}

bool validateSensorCommunityRecord(const BackendRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (!isNullTerminated(record.sensor_community_device_id, kBackendIdentifierCapacity)) {
        error = "Sensor.Community device ID is not null-terminated.";
        return false;
    }

    return validateHttpEndpoint(record, error);
}

bool validateHttpBackendRecord(const BackendRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }
    return validateHttpEndpoint(record, error);
}

bool validateCustomUploadRecord(const BackendRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.enabled == 0U) {
        error.clear();
        return true;
    }

    return validateHttpEndpoint(record, error);
}

bool validateInfluxDbRecord(const BackendRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (!validateHttpEndpoint(record, error)) {
        return false;
    }

    if (!isNullTerminated(record.influxdb_measurement, kBackendMeasurementCapacity) ||
        record.influxdb_measurement[0] == '\0') {
        error = "InfluxDB measurement name must not be empty.";
        return false;
    }

    return true;
}

static_assert(sizeof(BackendTypeDefaults) == 16U,
    "BackendTypeDefaults layout changed — update kDescriptors[] designated initializers");
static_assert(sizeof(BackendDescriptor) == 36U,
    "BackendDescriptor layout changed — update kDescriptors[] designated initializers");

constexpr BackendDescriptor kDescriptors[] = {
    {
        .type           = BackendType::kSensorCommunity,
        .backend_key    = "sensor_community",
        .display_name   = "Sensor.Community",
        .defaults       = {
            .host          = "api.sensor.community",
            .path          = "/v1/push-sensor-data/",
            .port          = 443U,
            .protocol      = BackendProtocol::kHttps,
            .host_is_fixed = true,
            .path_is_fixed = true,
        },
        .validate        = &validateSensorCommunityRecord,
        .create_uploader = &createSensorCommunityUploader,
    },
    {
        .type           = BackendType::kAir360Api,
        .backend_key    = "air360_api",
        .display_name   = "Air360 API",
        .defaults       = {
            .host          = "api.air360.ru",
            .path          = "/v1/devices/{chip_id}/batches/{batch_id}",
            .port          = 443U,
            .protocol      = BackendProtocol::kHttps,
            .host_is_fixed = true,
            .path_is_fixed = true,
        },
        .validate        = &validateHttpBackendRecord,
        .create_uploader = &createAir360ApiUploader,
    },
    {
        .type           = BackendType::kCustomUpload,
        .backend_key    = "custom_upload",
        .display_name   = "Custom Upload",
        .defaults       = {
            .host          = "",
            .path          = "",
            .port          = 443U,
            .protocol      = BackendProtocol::kHttps,
            .host_is_fixed = false,
            .path_is_fixed = false,
        },
        .validate        = &validateCustomUploadRecord,
        .create_uploader = &createCustomUploadUploader,
    },
    {
        .type           = BackendType::kInfluxDb,
        .backend_key    = "influxdb",
        .display_name   = "InfluxDB",
        .defaults       = {
            .host          = "",
            .path          = "",
            .port          = 443U,
            .protocol      = BackendProtocol::kHttps,
            .host_is_fixed = false,
            .path_is_fixed = false,
        },
        .validate        = &validateInfluxDbRecord,
        .create_uploader = &createInfluxDbUploader,
    },
};

}  // namespace

const BackendDescriptor* BackendRegistry::descriptors() const {
    return kDescriptors;
}

std::size_t BackendRegistry::descriptorCount() const {
    return sizeof(kDescriptors) / sizeof(kDescriptors[0]);
}

const BackendDescriptor* BackendRegistry::findByType(BackendType type) const {
    for (const auto& descriptor : kDescriptors) {
        if (descriptor.type == type) {
            return &descriptor;
        }
    }
    return nullptr;
}

const BackendDescriptor* BackendRegistry::findByKey(const std::string& backend_key) const {
    for (const auto& descriptor : kDescriptors) {
        if (backend_key == descriptor.backend_key) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool BackendRegistry::validateRecord(const BackendRecord& record, std::string& error) const {
    const BackendDescriptor* descriptor = findByType(record.backend_type);
    if (descriptor == nullptr) {
        error = "Unsupported backend type.";
        return false;
    }

    if (record.enabled == 0U) {
        error.clear();
        return true;
    }

    if (descriptor->validate == nullptr) {
        error.clear();
        return true;
    }

    return descriptor->validate(record, error);
}

}  // namespace air360
