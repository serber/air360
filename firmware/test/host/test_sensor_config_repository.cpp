// SensorConfigRepository against the in-memory NVS stub and the fake sensor
// registry (which accepts SHT4x and INA219 over I2C).
#include "air360/sensors/sensor_config_repository.hpp"
#include "nvs.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using air360::SensorConfigList;
using air360::SensorConfigRepository;
using air360::SensorType;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void resetNvs() {
    nvs_test::blobs.clear();
    nvs_test::writes = 0U;
    nvs_test::fail_write = false;
    nvs_test::fail_commit = false;
    nvs_test::fail_read = false;
}

SensorConfigList load(bool& loaded, bool& defaults) {
    SensorConfigList config{};
    require(SensorConfigRepository{}.loadOrCreate(config, loaded, defaults) == ESP_OK,
            "loadOrCreate succeeds");
    return config;
}

void storeRaw(const SensorConfigList& config) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&config);
    nvs_test::blobs["sensor_cfg"] = std::vector<std::uint8_t>(bytes, bytes + sizeof(config));
}

SensorConfigList withSensors(std::initializer_list<SensorType> types) {
    SensorConfigList config = air360::makeDefaultSensorConfigList();
    std::uint32_t id = 1U;
    for (const SensorType type : types) {
        auto& record = config.sensors[config.sensor_count++];
        record.id = id++;
        record.enabled = 1U;
        record.sensor_type = type;
        record.transport_kind = air360::TransportKind::kI2c;
        record.poll_interval_ms = 60000U;
    }
    config.next_sensor_id = id;
    return config;
}

void testEmptyNvsWritesDefaults() {
    resetNvs();
    bool loaded = true;
    bool defaults = false;
    const SensorConfigList config = load(loaded, defaults);
    require(!loaded && defaults && config.sensor_count == 0U, "empty NVS creates defaults");
    require(nvs_test::blobs.count("sensor_cfg") == 1U, "defaults are persisted");
}

void testRoundTripAndRejection() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    load(loaded, defaults);

    const SensorConfigList config = withSensors({SensorType::kSht4x, SensorType::kIna219});
    require(SensorConfigRepository{}.save(config) == ESP_OK, "valid config saves");
    const SensorConfigList reloaded = load(loaded, defaults);
    require(loaded && !defaults && reloaded.sensor_count == 2U &&
                reloaded.sensors[1].sensor_type == SensorType::kIna219,
            "config round-trips");

    const auto stored = nvs_test::blobs.at("sensor_cfg");
    require(SensorConfigRepository{}.save(withSensors({SensorType::kBme680})) ==
                ESP_ERR_INVALID_ARG,
            "unsupported sensor type rejected");
    SensorConfigList overflow = config;
    overflow.sensor_count = static_cast<std::uint16_t>(overflow.sensors.size() + 1U);
    require(SensorConfigRepository{}.save(overflow) == ESP_ERR_INVALID_ARG,
            "sensor_count beyond capacity rejected");
    require(nvs_test::blobs.at("sensor_cfg") == stored, "rejected saves leave NVS untouched");
}

void testWriteFailureKeepsPreviousConfig() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    require(SensorConfigRepository{}.save(withSensors({SensorType::kSht4x})) == ESP_OK,
            "baseline save");
    nvs_test::fail_write = true;
    require(SensorConfigRepository{}.save(withSensors({SensorType::kSht4x, SensorType::kIna219})) !=
                ESP_OK,
            "write failure propagates");
    nvs_test::fail_write = false;
    require(load(loaded, defaults).sensor_count == 1U, "previous config survives the failure");
}

void testRetiredSensorIsDroppedNotWholeList() {
    resetNvs();
    SensorConfigList config = withSensors({SensorType::kSht4x, SensorType::kMe3No2,
                                           SensorType::kIna219});
    storeRaw(config);
    bool loaded = false;
    bool defaults = false;
    const SensorConfigList reloaded = load(loaded, defaults);
    require(loaded && !defaults, "list with a retired sensor still loads");
    require(reloaded.sensor_count == 2U && reloaded.sensors[0].sensor_type == SensorType::kSht4x &&
                reloaded.sensors[1].sensor_type == SensorType::kIna219,
            "only the retired sensor is removed, order kept");
    SensorConfigList persisted{};
    std::memcpy(&persisted, nvs_test::blobs.at("sensor_cfg").data(), sizeof(persisted));
    require(persisted.sensor_count == 2U, "cleaned list is written back");
}

void testCorruptBlobFallsBackToDefaults() {
    resetNvs();
    SensorConfigList config = withSensors({SensorType::kSht4x});
    config.magic ^= 0xFFFFFFFFU;
    storeRaw(config);
    bool loaded = false;
    bool defaults = false;
    load(loaded, defaults);
    require(!loaded && defaults, "bad magic replaced with defaults");

    resetNvs();
    nvs_test::blobs["sensor_cfg"] = std::vector<std::uint8_t>(5U, 0x55U);
    load(loaded, defaults);
    require(!loaded && defaults, "wrong size replaced with defaults");
}

}  // namespace

int main() {
    testEmptyNvsWritesDefaults();
    testRoundTripAndRejection();
    testWriteFailureKeepsPreviousConfig();
    testRetiredSensorIsDroppedNotWholeList();
    testCorruptBlobFallsBackToDefaults();
    std::cout << "sensor config repository tests passed\n";
    return 0;
}
