#include "air360/uploads/backend_config_repository.hpp"
#include "nvs.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using air360::BackendConfigList;
using air360::BackendConfigRepository;
using air360::BackendType;

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

BackendConfigList loadFresh(bool& loaded, bool& defaults) {
    BackendConfigList config{};
    require(BackendConfigRepository{}.loadOrCreate(config, loaded, defaults) == ESP_OK,
            "loadOrCreate succeeds");
    return config;
}

// Writes raw bytes as the stored blob, bypassing save() validation, to model
// blobs written by older firmware or corrupted in flash.
void storeRaw(const BackendConfigList& config) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&config);
    nvs_test::blobs["backend_cfg"] = std::vector<std::uint8_t>(bytes, bytes + sizeof(config));
}

void testEmptyNvsWritesDefaults() {
    resetNvs();
    bool loaded = true;
    bool defaults = false;
    const BackendConfigList config = loadFresh(loaded, defaults);
    require(!loaded && defaults, "empty NVS creates defaults");
    require(config.backend_count > 0U, "defaults contain every backend type");
    require(air360::findBackendRecordByType(config, BackendType::kAir360Api) != nullptr,
            "Air360 API record exists by default");
    require(nvs_test::blobs.count("backend_cfg") == 1U, "defaults are persisted");
}

void testSaveRoundTrip() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    BackendConfigList config = loadFresh(loaded, defaults);
    config.upload_interval_ms = 600000U;
    auto* air360 = air360::findBackendRecordByType(config, BackendType::kAir360Api);
    air360->latitude = 55.75F;
    air360->longitude = 37.62F;
    air360->altitude_m = 150.0F;
    require(BackendConfigRepository{}.save(config) == ESP_OK, "valid config saves");

    const BackendConfigList reloaded = loadFresh(loaded, defaults);
    require(loaded && !defaults, "saved config is loaded from storage");
    require(reloaded.upload_interval_ms == 600000U, "interval round-trips");
    const auto* reloaded_air360 =
        air360::findBackendRecordByType(reloaded, BackendType::kAir360Api);
    require(reloaded_air360->latitude == 55.75F && reloaded_air360->longitude == 37.62F &&
                reloaded_air360->altitude_m == 150.0F,
            "location round-trips");
}

void testSaveRejectsInvalidConfigWithoutWriting() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    BackendConfigList config = loadFresh(loaded, defaults);
    const auto stored = nvs_test::blobs.at("backend_cfg");

    BackendConfigList bad_interval = config;
    bad_interval.upload_interval_ms = air360::kMinUploadIntervalMs - 1U;
    require(BackendConfigRepository{}.save(bad_interval) == ESP_ERR_INVALID_ARG,
            "interval below minimum rejected");

    BackendConfigList bad_location = config;
    air360::findBackendRecordByType(bad_location, BackendType::kAir360Api)->latitude =
        std::numeric_limits<float>::quiet_NaN();
    require(BackendConfigRepository{}.save(bad_location) == ESP_ERR_INVALID_ARG,
            "NaN latitude rejected even for a disabled record");

    BackendConfigList out_of_range = config;
    air360::findBackendRecordByType(out_of_range, BackendType::kAir360Api)->longitude = 181.0F;
    require(BackendConfigRepository{}.save(out_of_range) == ESP_ERR_INVALID_ARG,
            "longitude above 180 rejected");

    require(nvs_test::blobs.at("backend_cfg") == stored, "rejected saves leave NVS untouched");
}

void testWriteFailurePropagatesAndKeepsOldConfig() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    BackendConfigList config = loadFresh(loaded, defaults);
    config.upload_interval_ms = 300000U;
    require(BackendConfigRepository{}.save(config) == ESP_OK, "baseline save");

    config.upload_interval_ms = 900000U;
    nvs_test::fail_write = true;
    require(BackendConfigRepository{}.save(config) != ESP_OK, "write failure propagates");
    nvs_test::fail_write = false;
    require(loadFresh(loaded, defaults).upload_interval_ms == 300000U,
            "failed write keeps the previous config");

    nvs_test::fail_commit = true;
    require(BackendConfigRepository{}.save(config) != ESP_OK, "commit failure propagates");
    nvs_test::fail_commit = false;
}

void testStoredNonFiniteLocationIsSanitizedNotDiscarded() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    BackendConfigList config = loadFresh(loaded, defaults);
    config.upload_interval_ms = 420000U;
    auto* air360 = air360::findBackendRecordByType(config, BackendType::kAir360Api);
    air360->latitude = std::numeric_limits<float>::infinity();
    air360->longitude = 37.62F;
    storeRaw(config);

    const BackendConfigList reloaded = loadFresh(loaded, defaults);
    require(loaded && !defaults, "blob with a bad location still loads");
    require(reloaded.upload_interval_ms == 420000U, "other settings survive sanitizing");
    const auto* sanitized = air360::findBackendRecordByType(reloaded, BackendType::kAir360Api);
    require(sanitized->latitude == 0.0F && sanitized->longitude == 0.0F &&
                sanitized->altitude_m == 0.0F,
            "bad location reset to not-set");

    BackendConfigList persisted{};
    std::memcpy(&persisted, nvs_test::blobs.at("backend_cfg").data(), sizeof(persisted));
    require(air360::hasValidAir360Location(
                *air360::findBackendRecordByType(persisted, BackendType::kAir360Api)),
            "sanitized location is written back");
}

void testCorruptBlobFallsBackToDefaults() {
    resetNvs();
    bool loaded = false;
    bool defaults = false;
    BackendConfigList config = loadFresh(loaded, defaults);
    config.magic ^= 0xFFFFFFFFU;
    storeRaw(config);
    loadFresh(loaded, defaults);
    require(!loaded && defaults, "bad magic is replaced with defaults");

    resetNvs();
    nvs_test::blobs["backend_cfg"] = std::vector<std::uint8_t>(7U, 0xAAU);
    loadFresh(loaded, defaults);
    require(!loaded && defaults, "wrong blob size is replaced with defaults");
}

}  // namespace

int main() {
    testEmptyNvsWritesDefaults();
    testSaveRoundTrip();
    testSaveRejectsInvalidConfigWithoutWriting();
    testWriteFailurePropagatesAndKeepsOldConfig();
    testStoredNonFiniteLocationIsSanitizedNotDiscarded();
    testCorruptBlobFallsBackToDefaults();
    std::cout << "backend config repository tests passed\n";
    return 0;
}
