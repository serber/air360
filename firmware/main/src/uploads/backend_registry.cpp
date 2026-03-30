#include "air360/uploads/backend_registry.hpp"

#include <cstring>
#include <string>

#include "air360/uploads/adapters/sensor_community_uploader.hpp"

namespace air360 {

namespace {

bool validateCommonRecord(const BackendRecord& record, std::string& error) {
    if (record.id == 0U) {
        error = "Backend id must not be zero.";
        return false;
    }

    if (record.display_name[0] == '\0') {
        error = "Backend display name must not be empty.";
        return false;
    }

    if (record.display_name[kBackendDisplayNameCapacity - 1U] != '\0') {
        error = "Backend display name is not null-terminated.";
        return false;
    }

    const std::size_t display_name_length = std::strlen(record.display_name);
    if (display_name_length == 0U || display_name_length >= kBackendDisplayNameCapacity) {
        error = "Backend display name is invalid.";
        return false;
    }

    return true;
}

bool validateSensorCommunityRecord(const BackendRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.endpoint_url[0] == '\0') {
        error = "Sensor.Community endpoint URL must not be empty.";
        return false;
    }

    return true;
}

bool validateAir360ApiRecord(const BackendRecord& record, std::string& error) {
    return validateCommonRecord(record, error);
}

constexpr BackendDescriptor kDescriptors[] = {
    {
        BackendType::kSensorCommunity,
        "sensor_community",
        "Sensor.Community",
        true,
        true,
        true,
        &validateSensorCommunityRecord,
        &createSensorCommunityUploader,
    },
    {
        BackendType::kAir360Api,
        "air360_api",
        "Air360 API",
        false,
        false,
        true,
        &validateAir360ApiRecord,
        nullptr,
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

    if (descriptor->validate == nullptr) {
        error.clear();
        return true;
    }

    return descriptor->validate(record, error);
}

}  // namespace air360
