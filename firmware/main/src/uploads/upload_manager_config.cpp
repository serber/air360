#include "air360/uploads/upload_manager.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include "air360/time_utils.hpp"

namespace air360 {

namespace {

std::string defaultDisplayName(
    const BackendDescriptor* descriptor,
    std::uint32_t id) {
    if (descriptor != nullptr && descriptor->display_name != nullptr) {
        return std::string(descriptor->display_name);
    }

    return std::string("Backend #") + std::to_string(id);
}

BackendRuntimeState classifyInitialState(bool enabled, bool configured) {
    if (!enabled) {
        return BackendRuntimeState::kDisabled;
    }
    if (!configured) {
        return BackendRuntimeState::kError;
    }
    return BackendRuntimeState::kIdle;
}

}  // namespace

std::vector<UploadManager::ManagedBackend> UploadManager::buildManagedBackends(
    const BackendConfigList& config) const {
    BackendRegistry registry;
    const std::uint64_t now_ms = uptimeMilliseconds();
    std::vector<ManagedBackend> backends;
    backends.reserve(config.backend_count);

    for (std::size_t index = 0; index < config.backend_count; ++index) {
        const BackendRecord& record = config.backends[index];
        const BackendDescriptor* descriptor = registry.findByType(record.backend_type);

        ManagedBackend managed;
        managed.record = record;
        managed.descriptor = descriptor;
        managed.snapshot.id = record.id;
        managed.snapshot.enabled = record.enabled != 0U;
        managed.snapshot.backend_type = record.backend_type;
        managed.snapshot.backend_key =
            descriptor != nullptr ? descriptor->backend_key : std::string("unknown");
        managed.snapshot.display_name =
            record.display_name[0] != '\0' ? std::string(record.display_name)
                                           : defaultDisplayName(descriptor, record.id);
        std::string validation_error;
        const bool configured =
            descriptor != nullptr && registry.validateRecord(record, validation_error);
        managed.snapshot.configured = configured;
        managed.snapshot.state = classifyInitialState(managed.snapshot.enabled, configured);

        if (!validation_error.empty()) {
            managed.snapshot.last_result = UploadResultClass::kConfigError;
            managed.snapshot.last_error = validation_error;
        }

        if (managed.snapshot.enabled &&
            configured &&
            descriptor != nullptr &&
            descriptor->create_uploader != nullptr) {
            managed.uploader = descriptor->create_uploader();
            if (!managed.uploader) {
                managed.snapshot.state = BackendRuntimeState::kError;
                managed.snapshot.last_result = UploadResultClass::kConfigError;
                managed.snapshot.last_error = "Failed to allocate backend uploader.";
            } else {
                managed.next_action_time_ms = now_ms;
            }
        }

        backends.push_back(std::move(managed));
    }

    return backends;
}

MeasurementBatch UploadManager::buildMeasurementBatch(
    std::uint64_t now_ms,
    const std::vector<MeasurementSample>& samples) const {
    MeasurementBatch batch;
    batch.created_uptime_ms = now_ms;
    batch.created_unix_ms = currentUnixMilliseconds();
    batch.batch_id = batch.created_unix_ms > 0 ? static_cast<std::uint64_t>(batch.created_unix_ms)
                                               : now_ms;
    batch.device_name = device_config_ != nullptr ? device_config_->device_name : "";
    batch.board_name = build_info_.board_name;
    batch.project_version = build_info_.project_version;
    batch.chip_id = build_info_.chip_id;
    batch.short_chip_id = build_info_.short_chip_id;
    batch.esp_mac_id = build_info_.esp_mac_id;

    if (network_manager_ != nullptr) {
        const NetworkState network = network_manager_->state();
        batch.network_mode = network.mode;
        batch.station_connected = network.station_connected;
    }

    for (const auto& sample : samples) {
        for (std::size_t index = 0; index < sample.measurement.value_count; ++index) {
            const SensorValue& value = sample.measurement.values[index];
            batch.points.push_back(
                MeasurementPoint{
                    sample.sensor_id,
                    sample.sensor_type,
                    value.kind,
                    value.value,
                    sample.sample_time_ms,
                });
        }
    }

    return batch;
}

bool UploadManager::hasNetworkForUpload(std::string& last_error) const {
    if (network_manager_ == nullptr) {
        last_error = "Network manager is not available.";
        return false;
    }

    if (network_manager_->uplinkStatus().uplink_ready) {
        last_error.clear();
        return true;
    }

    // Bearer is up but time is not yet valid: provide a specific message.
    const NetworkState network = network_manager_->state();
    const bool bearer_up = (network.mode == NetworkMode::kStation && network.station_connected);
    if (bearer_up) {
        if (!network.time_sync_error.empty()) {
            last_error = "Unix time is not valid yet: " + network.time_sync_error;
        } else {
            last_error = "Unix time is not valid yet.";
        }
    } else {
        last_error = "Uplink is not ready.";
    }
    return false;
}

}  // namespace air360
