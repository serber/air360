#include "air360/sensors/sensor_config_store.hpp"

#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using air360::MaintenanceActionKind;
using air360::SensorConfigList;
using air360::SensorConfigStore;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

constexpr auto kNone = static_cast<std::uint8_t>(MaintenanceActionKind::kNone);
constexpr auto kFrc = static_cast<std::uint8_t>(MaintenanceActionKind::kForcedRecalibration);
constexpr auto kFanClean = static_cast<std::uint8_t>(MaintenanceActionKind::kFanClean);

// Fake NVS: remembers the last persisted blob and can be told to fail.
struct FakeNvs {
    SensorConfigList persisted = air360::makeDefaultSensorConfigList();
    int writes = 0;
    bool fail = false;
};

SensorConfigList twoSensors(std::uint8_t action_for_1) {
    SensorConfigList config = air360::makeDefaultSensorConfigList();
    config.sensor_count = 2U;
    config.sensors[0].id = 1U;
    config.sensors[0].poll_interval_ms = 60000U;
    config.sensors[0].pending_maintenance_action = action_for_1;
    config.sensors[1].id = 2U;
    config.sensors[1].poll_interval_ms = 60000U;
    config.next_sensor_id = 3U;
    return config;
}

std::uint8_t actionOf(const SensorConfigList& config, std::uint32_t id) {
    const auto* record = air360::findSensorRecordById(config, id);
    return record == nullptr ? 0xFFU : record->pending_maintenance_action;
}

void bind(SensorConfigStore& store, FakeNvs& nvs, const SensorConfigList& initial) {
    nvs.persisted = initial;
    store.reset(initial, [&nvs](const SensorConfigList& config) {
        if (nvs.fail) {
            return ESP_FAIL;
        }
        nvs.persisted = config;
        ++nvs.writes;
        return ESP_OK;
    });
}

void testCompletionClearsAndPersists() {
    SensorConfigStore store;
    FakeNvs nvs;
    bind(store, nvs, twoSensors(kFrc));
    const std::uint32_t generation = store.generation();

    bool cleared = false;
    require(store.clearMaintenanceAction(1U, generation, cleared) == ESP_OK, "clear ok");
    require(cleared, "current-generation completion clears the action");
    SensorConfigList live;
    store.copyTo(live);
    require(actionOf(live, 1U) == kNone, "live action cleared");
    require(actionOf(nvs.persisted, 1U) == kNone, "NVS matches live");

    require(store.clearMaintenanceAction(1U, generation, cleared) == ESP_OK && !cleared,
            "second completion is a no-op");
}

void testStaleCompletionDoesNotClearNewRequest() {
    // FRC runs; the operator schedules fan cleaning on the same sensor and
    // applies before the old FRC completion is reported.
    SensorConfigStore store;
    FakeNvs nvs;
    bind(store, nvs, twoSensors(kFrc));
    const std::uint32_t old_generation = store.generation();

    SensorConfigList base;
    store.copyTo(base);
    SensorConfigList staged = base;
    air360::findSensorRecordById(staged, 1U)->pending_maintenance_action = kFanClean;
    std::uint32_t new_generation = 0U;
    require(store.replace(staged, base, new_generation) == ESP_OK, "apply ok");
    require(new_generation != old_generation, "apply bumps generation");

    bool cleared = false;
    require(store.clearMaintenanceAction(1U, old_generation, cleared) == ESP_OK, "stale ok");
    require(!cleared, "stale completion is ignored");
    SensorConfigList live;
    store.copyTo(live);
    require(actionOf(live, 1U) == kFanClean, "new request survives");
    require(actionOf(nvs.persisted, 1U) == kFanClean, "NVS keeps new request");
}

void testStaleStagedCopyDoesNotReArmFinishedAction() {
    // Operator starts editing sensor #2 while FRC is pending on #1; FRC then
    // completes; Apply must not bring the finished FRC back.
    SensorConfigStore store;
    FakeNvs nvs;
    bind(store, nvs, twoSensors(kFrc));

    SensorConfigList base;
    store.copyTo(base);
    SensorConfigList staged = base;
    air360::findSensorRecordById(staged, 2U)->poll_interval_ms = 120000U;

    bool cleared = false;
    require(store.clearMaintenanceAction(1U, store.generation(), cleared) == ESP_OK && cleared,
            "completion during edit");

    std::uint32_t generation = 0U;
    require(store.replace(staged, base, generation) == ESP_OK, "apply ok");
    require(actionOf(staged, 1U) == kNone, "accepted config reflects completion");
    require(air360::findSensorRecordById(staged, 2U)->poll_interval_ms == 120000U,
            "operator edit kept");
    require(actionOf(nvs.persisted, 1U) == kNone, "NVS does not re-arm FRC");
}

void testDeletedSensorCompletionIsIgnored() {
    SensorConfigStore store;
    FakeNvs nvs;
    bind(store, nvs, twoSensors(kFrc));
    const std::uint32_t old_generation = store.generation();

    SensorConfigList base;
    store.copyTo(base);
    SensorConfigList staged = base;
    require(air360::eraseSensorRecordById(staged, 1U), "delete in staged");
    std::uint32_t generation = 0U;
    require(store.replace(staged, base, generation) == ESP_OK, "apply ok");
    const int writes_after_apply = nvs.writes;

    bool cleared = false;
    require(store.clearMaintenanceAction(1U, old_generation, cleared) == ESP_OK && !cleared,
            "completion for deleted sensor ignored");
    require(store.clearMaintenanceAction(1U, generation, cleared) == ESP_OK && !cleared,
            "even with the current generation");
    require(nvs.writes == writes_after_apply, "no extra NVS write");
    require(actionOf(nvs.persisted, 1U) == 0xFFU, "deleted sensor stays deleted");
}

void testPersistFailureLeavesLiveUnchanged() {
    SensorConfigStore store;
    FakeNvs nvs;
    bind(store, nvs, twoSensors(kFrc));
    const std::uint32_t generation = store.generation();
    nvs.fail = true;

    bool cleared = true;
    require(store.clearMaintenanceAction(1U, generation, cleared) == ESP_FAIL && !cleared,
            "clear reports persist failure");
    SensorConfigList live;
    store.copyTo(live);
    require(actionOf(live, 1U) == kFrc, "clear rolled back");

    SensorConfigList staged = live;
    air360::findSensorRecordById(staged, 2U)->poll_interval_ms = 90000U;
    std::uint32_t new_generation = 0U;
    require(store.replace(staged, live, new_generation) == ESP_FAIL, "apply reports failure");
    require(new_generation == generation, "generation unchanged on failure");
    store.copyTo(live);
    require(air360::findSensorRecordById(live, 2U)->poll_interval_ms == 60000U,
            "live unchanged on failure");
}

void testConcurrentApplyAndCompletionKeepNvsInSync() {
    // Real threads against the mutex-backed semaphore stub: the main thread
    // ("HTTP task") keeps applying configs that re-arm actions while
    // "sensor task" threads report completions. Whatever the interleaving,
    // the persisted blob must equal the live config, the last Apply's operator
    // edit must survive, and ThreadSanitizer must see no data race. Stale
    // generations are covered deterministically above.
    SensorConfigStore store;
    FakeNvs nvs;
    bind(store, nvs, twoSensors(kFrc));

    std::atomic<bool> done{false};
    std::vector<std::thread> completers;
    for (int worker = 0; worker < 3; ++worker) {
        completers.emplace_back([&store, &done] {
            while (!done.load()) {
                bool cleared = false;
                require(store.clearMaintenanceAction(1U, store.generation(), cleared) == ESP_OK,
                        "concurrent clear ok");
            }
        });
    }

    for (int round = 0; round < 2000; ++round) {
        SensorConfigList base;
        store.copyTo(base);
        SensorConfigList staged = base;
        air360::findSensorRecordById(staged, 1U)->pending_maintenance_action =
            (round % 2 == 0) ? kFanClean : kFrc;
        air360::findSensorRecordById(staged, 2U)->poll_interval_ms =
            60000U + static_cast<std::uint32_t>(round);
        std::uint32_t generation = 0U;
        require(store.replace(staged, base, generation) == ESP_OK, "concurrent apply ok");
    }
    done.store(true);
    for (auto& thread : completers) {
        thread.join();
    }

    SensorConfigList live;
    store.copyTo(live);
    require(actionOf(live, 1U) == actionOf(nvs.persisted, 1U), "NVS action matches live");
    require(air360::findSensorRecordById(live, 2U)->poll_interval_ms ==
                air360::findSensorRecordById(nvs.persisted, 2U)->poll_interval_ms,
            "NVS operator edit matches live");
    require(air360::findSensorRecordById(live, 2U)->poll_interval_ms == 60000U + 1999U,
            "last Apply wins");
}

}  // namespace

int main() {
    testCompletionClearsAndPersists();
    testStaleCompletionDoesNotClearNewRequest();
    testStaleStagedCopyDoesNotReArmFinishedAction();
    testDeletedSensorCompletionIsIgnored();
    testPersistFailureLeavesLiveUnchanged();
    testConcurrentApplyAndCompletionKeepNvsInSync();
    std::cout << "sensor config store tests passed\n";
    return 0;
}
