// SensorManager lifecycle on emulated FreeRTOS tasks (std::thread), with the
// scriptable fake sensor registry. Covers start/stop/Apply, the
// after-network startup phase, poll-failure re-init, stop during a slow poll,
// and the sample-freshness rule (#75).
#include "air360/sensors/sensor_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

#include "esp_timer.h"
#include "fake_sensor_registry.hpp"
#include "fake_transport.hpp"
#include "freertos/task.h"

namespace {

using air360::SensorConfigList;
using air360::SensorManager;
using air360::SensorRuntimeState;
using air360::SensorType;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Polls a condition on the test thread; the manager runs on its own thread.
bool waitUntil(const std::function<bool()>& condition, int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

SensorRuntimeState stateOf(const SensorManager& manager, std::uint32_t id) {
    for (const auto& sensor : manager.sensors()) {
        if (sensor.id == id) {
            return sensor.state;
        }
    }
    return SensorRuntimeState::kUnsupported;
}

air360::SensorRecord record(std::uint32_t id, SensorType type) {
    air360::SensorRecord sensor{};
    sensor.id = id;
    sensor.enabled = 1U;
    sensor.sensor_type = type;
    sensor.transport_kind = air360::TransportKind::kI2c;
    sensor.poll_interval_ms = 60000U;
    return sensor;
}

SensorConfigList config(std::initializer_list<air360::SensorRecord> records) {
    SensorConfigList list = air360::makeDefaultSensorConfigList();
    list.sensor_count = 0U;
    for (const auto& sensor : records) {
        list.sensors[list.sensor_count++] = sensor;
    }
    return list;
}

// Poll intervals are in uptime; jumping the clock makes the next pass due.
void advanceToNextPoll() {
    esp_timer_test::advanceMs(6000);
}

void testStartPhasesAndStop() {
    fake_sensors::reset();
    air360::MeasurementStore store;
    SensorManager manager;
    manager.setMeasurementStore(store);

    // INA219 is kBeforeNetwork, SHT4x is kAfterNetwork.
    require(manager.applyConfig(config({record(1U, SensorType::kIna219),
                                        record(2U, SensorType::kSht4x)}), 1U) == ESP_OK,
            "apply starts the task");
    require(waitUntil([] { return fake_sensors::behaviour(1U).init_calls.load() >= 1; }),
            "before-network sensor is initialized");
    require(stateOf(manager, 2U) == SensorRuntimeState::kDeferred,
            "after-network sensor stays parked before release");
    require(fake_sensors::behaviour(2U).init_calls.load() == 0, "parked sensor not touched");

    manager.releaseAfterNetworkPhase();
    require(waitUntil([] { return fake_sensors::behaviour(2U).init_calls.load() >= 1; }),
            "after-network sensor starts after release");
    advanceToNextPoll();
    require(waitUntil([&] { return store.runtimeInfoForSensor(2U).measurement.value_count > 0U; }),
            "after-network sensor reports a latest measurement");

    const int running_before = freertos_test::running_tasks.load();
    require(manager.stop() == ESP_OK, "stop is acknowledged");
    require(waitUntil([&] { return freertos_test::running_tasks.load() == running_before - 1; }),
            "sensor task exits after stop");
}

void testApplyReplacesDriversAndReleasesUart() {
    fake_sensors::reset();
    air360::MeasurementStore store;
    SensorManager manager;
    manager.setMeasurementStore(store);
    manager.releaseAfterNetworkPhase();

    require(manager.applyConfig(config({record(1U, SensorType::kSht4x)}), 1U) == ESP_OK,
            "first apply");
    require(waitUntil([] { return fake_sensors::behaviour(1U).init_calls.load() >= 1; }),
            "first driver initialized");

    const int shutdowns_before = fake_transport::uart_shutdown_calls.load();
    require(manager.applyConfig(config({record(3U, SensorType::kSht4x)}), 2U) == ESP_OK,
            "second apply");
    require(fake_transport::uart_shutdown_calls.load() > shutdowns_before,
            "Apply shuts down UART ports of the stopped task");
    require(fake_sensors::behaviour(1U).drivers_destroyed.load() == 1,
            "replaced sensor's driver is destroyed");
    require(waitUntil([] { return fake_sensors::behaviour(3U).init_calls.load() >= 1; }),
            "new sensor initialized after Apply");
    require(manager.sensors().size() == 1U && manager.sensors()[0].id == 3U,
            "runtime list reflects the new config");
    require(manager.stop() == ESP_OK, "stop");
}

void testStopDuringSlowPoll() {
    fake_sensors::reset();
    air360::MeasurementStore store;
    SensorManager manager;
    manager.setMeasurementStore(store);
    manager.releaseAfterNetworkPhase();

    require(manager.applyConfig(config({record(1U, SensorType::kSht4x)}), 1U) == ESP_OK, "apply");
    require(waitUntil([] { return fake_sensors::behaviour(1U).init_calls.load() >= 1; }), "init");
    fake_sensors::behaviour(1U).poll_delay_ms = 800;
    advanceToNextPoll();
    require(waitUntil([] { return fake_sensors::behaviour(1U).poll_calls.load() >= 1; }),
            "slow poll started");

    const auto started = std::chrono::steady_clock::now();
    require(manager.stop() == ESP_OK, "stop waits for the in-flight poll");
    const auto waited = std::chrono::steady_clock::now() - started;
    require(waited < std::chrono::seconds(5), "stop returns within the stop timeout");
}

void testPollFailuresTriggerReinit() {
    fake_sensors::reset();
    air360::MeasurementStore store;
    SensorManager manager;
    manager.setMeasurementStore(store);
    manager.releaseAfterNetworkPhase();

    require(manager.applyConfig(config({record(1U, SensorType::kSht4x)}), 1U) == ESP_OK, "apply");
    auto& sensor = fake_sensors::behaviour(1U);
    require(waitUntil([&] { return sensor.init_calls.load() == 1; }), "initial init");

    sensor.poll_result = ESP_FAIL;
    // Soft failures retry after <= 5 s; the third consecutive failure drops
    // the driver back to init with a backoff.
    for (int attempt = 0; attempt < 10 && sensor.init_calls.load() < 2; ++attempt) {
        advanceToNextPoll();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    require(sensor.init_calls.load() >= 2, "repeated poll failures re-initialize the driver");

    sensor.poll_result = ESP_OK;
    for (int attempt = 0; attempt < 10 && stateOf(manager, 1U) != SensorRuntimeState::kPolling;
         ++attempt) {
        advanceToNextPoll();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    require(stateOf(manager, 1U) == SensorRuntimeState::kPolling, "sensor recovers to polling");
    require(manager.stop() == ESP_OK, "stop");
}

void testCachedSampleIsQueuedOnce() {
    fake_sensors::reset();
    air360::MeasurementStore store;
    SensorManager manager;
    manager.setMeasurementStore(store);
    manager.releaseAfterNetworkPhase();

    require(manager.applyConfig(config({record(1U, SensorType::kSht4x)}), 1U) == ESP_OK, "apply");
    auto& sensor = fake_sensors::behaviour(1U);
    require(waitUntil([&] { return sensor.init_calls.load() == 1; }), "init");

    // Leave the 60 s warmup window, where readings are not queued.
    esp_timer_test::advanceMs(61000);
    sensor.fresh_samples = false;
    const std::size_t pending_before = store.pendingCount();
    const int polls_before = sensor.poll_calls.load();
    for (int attempt = 0; attempt < 5; ++attempt) {
        esp_timer_test::advanceMs(61000);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    require(sensor.poll_calls.load() >= polls_before + 3, "sensor kept polling");
    require(store.pendingCount() <= pending_before + 1U,
            "a cached reading is queued at most once");

    sensor.fresh_samples = true;
    const std::size_t pending_cached = store.pendingCount();
    esp_timer_test::advanceMs(61000);
    require(waitUntil([&] { return store.pendingCount() > pending_cached; }),
            "fresh samples are queued again");
    require(manager.stop() == ESP_OK, "stop");
}

}  // namespace

int main() {
    esp_timer_test::real_time = true;
    testStartPhasesAndStop();
    testApplyReplacesDriversAndReleasesUart();
    testStopDuringSlowPoll();
    testPollFailuresTriggerReinit();
    testCachedSampleIsQueuedOnce();
    std::cout << "sensor manager lifecycle tests passed\n";
    return 0;
}
