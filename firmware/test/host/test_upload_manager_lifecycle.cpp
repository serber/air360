// UploadManager lifecycle on emulated FreeRTOS tasks with the real backend
// registry, uploaders and UploadTransport over the scripted HTTP stub.
#include "air360/uploads/upload_manager.hpp"
#include "air360/time_utils.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "esp_http_client.h"
#include "esp_timer.h"
#include "fake_network.hpp"

namespace {

using air360::BackendConfigList;
using air360::BackendType;
using air360::UploadManager;

const UploadManager* g_debug_manager = nullptr;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        if (g_debug_manager != nullptr) {
            for (const auto& backend : g_debug_manager->backends()) {
                std::cerr << "  backend " << backend.backend_key << " enabled=" << backend.enabled
                          << " configured=" << backend.configured
                          << " state=" << static_cast<int>(backend.state)
                          << " error=" << backend.last_error << '\n';
            }
        }
        std::lock_guard<std::mutex> guard(http_test::mutex);
        for (const auto& request : http_test::requests) {
            std::cerr << "  request " << request.url << " body=" << request.body.substr(0, 120)
                      << '\n';
        }
        std::exit(1);
    }
}

bool waitUntil(const std::function<bool()>& condition, int timeout_ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

// Distinct float values tag each sample so request bodies show which
// samples a backend delivered.
void recordSample(air360::MeasurementStore& store, std::uint32_t sensor_id, float value) {
    air360::SensorMeasurement measurement;
    measurement.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    measurement.addValue(air360::SensorValueKind::kTemperatureC, value);
    store.recordMeasurement(sensor_id, air360::SensorType::kSht4x, measurement,
                            air360::currentUnixMilliseconds());
}

int requestsTo(const std::string& host, const std::string& needle = "") {
    std::lock_guard<std::mutex> guard(http_test::mutex);
    int count = 0;
    for (const auto& request : http_test::requests) {
        if (request.url.find(host) != std::string::npos &&
            (needle.empty() || request.body.find(needle) != std::string::npos)) {
            ++count;
        }
    }
    return count;
}

// Custom Upload -> fast.test (succeeds); InfluxDB -> slow.test (fails, so it
// keeps every sample retained in the shared queue).
BackendConfigList twoBackends() {
    BackendConfigList config = air360::makeDefaultBackendConfigList();
    config.upload_interval_ms = air360::kMinUploadIntervalMs;
    for (std::size_t index = 0; index < config.backend_count; ++index) {
        auto& record = config.backends[index];
        record.enabled = 0U;
        record.protocol = air360::BackendProtocol::kHttp;
        record.port = 80U;
        if (record.backend_type == BackendType::kCustomUpload) {
            record.enabled = 1U;
            std::strcpy(record.host, "fast.test");
            std::strcpy(record.path, "/upload");
        } else if (record.backend_type == BackendType::kInfluxDb) {
            record.enabled = 1U;
            std::strcpy(record.host, "influx.test");
            std::strcpy(record.path, "/write");
            std::strcpy(record.influxdb_measurement, "air360");
        }
    }
    return config;
}

struct Harness {
    air360::BuildInfo build_info{};
    air360::DeviceConfig device_config{};
    air360::SensorManager sensor_manager;
    air360::MeasurementStore store;
    air360::NetworkManager network_manager;
    air360::Air360ApiCredentialRepository credentials;
    air360::OpenSenseMapMappingRepository mappings;
    UploadManager manager;

    Harness() {
        build_info.device_id = "112233445566";
        build_info.short_device_id = "445566";
        build_info.project_version = "test";
        manager.start(build_info, device_config, sensor_manager, store, network_manager,
                      credentials, mappings);
        g_debug_manager = &manager;
    }
};

void testDeliversAndStops() {
    http_test::reset();
    {
        std::lock_guard<std::mutex> guard(http_test::mutex);
        http_test::status_by_url_substring = {{"influx.test", 500}};
    }
    Harness harness;
    recordSample(harness.store, 1U, 4100.5F);
    require(harness.manager.applyConfig(twoBackends()) == ESP_OK, "apply starts the task");

    esp_timer_test::advanceMs(120000);  // past any initial backend delay
    require(waitUntil([] { return requestsTo("fast.test", "4100.5") >= 1; }),
            "succeeding backend delivers the sample");
    require(waitUntil([] { return requestsTo("influx.test") >= 1; }),
            "failing backend attempts delivery");
    require(harness.store.pendingCount() == 1U,
            "sample stays queued while the failing backend has not acknowledged it");
    require(harness.manager.stop() == ESP_OK, "stop is acknowledged");
}

// #83: the cursor of a backend whose request was in flight during Apply must
// be taken after the task stopped, so that window is not delivered again.
void testApplyDuringInflightRequestDoesNotResend() {
    http_test::reset();
    {
        std::lock_guard<std::mutex> guard(http_test::mutex);
        http_test::status_by_url_substring = {{"influx.test", 500}};
    }
    Harness harness;
    recordSample(harness.store, 1U, 4200.5F);
    require(harness.manager.applyConfig(twoBackends()) == ESP_OK, "first apply");

    http_test::perform_delay_ms = 600;
    const int calls_before = http_test::perform_calls.load();
    esp_timer_test::advanceMs(120000);
    require(waitUntil([&] { return http_test::perform_calls.load() > calls_before; }),
            "a request is in flight");

    // Apply while the request is still being performed.
    require(harness.manager.applyConfig(twoBackends()) == ESP_OK, "apply during in-flight request");
    http_test::perform_delay_ms = 0;
    require(waitUntil([] { return requestsTo("fast.test", "4200.5") >= 1; }),
            "in-flight window was delivered");

    // Run several upload cycles of the rebuilt backends. The failing backend
    // retries every cycle, so its request count marks each completed cycle
    // (the task idles up to 1 s between passes).
    for (int cycle = 0; cycle < 3; ++cycle) {
        const int influx_before = requestsTo("influx.test");
        esp_timer_test::advanceMs(120000);
        require(waitUntil([&] { return requestsTo("influx.test") > influx_before; }),
                "failing backend keeps retrying (sample retained)");
    }
    require(requestsTo("fast.test", "4200.5") == 1,
            "acknowledged window is not resent after Apply");

    recordSample(harness.store, 1U, 4300.5F);
    esp_timer_test::advanceMs(120000);
    require(waitUntil([] { return requestsTo("fast.test", "4300.5") >= 1; }),
            "new samples still flow to the succeeding backend");
    require(harness.manager.stop() == ESP_OK, "stop");
}

void testNoUplinkNoRequests() {
    http_test::reset();
    fake_network::uplink_ready = false;
    Harness harness;
    recordSample(harness.store, 1U, 4400.5F);
    require(harness.manager.applyConfig(twoBackends()) == ESP_OK, "apply");
    // Several due cycles; each pause outlasts the 1 s idle wait of the task.
    for (int cycle = 0; cycle < 3; ++cycle) {
        esp_timer_test::advanceMs(120000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }
    require(http_test::perform_calls.load() == 0, "no HTTP request without an uplink");
    fake_network::uplink_ready = true;
    esp_timer_test::advanceMs(120000);
    require(waitUntil([] { return requestsTo("fast.test", "4400.5") >= 1; }),
            "delivery resumes when the uplink returns");
    require(harness.manager.stop() == ESP_OK, "stop");
}

}  // namespace

int main() {
    esp_timer_test::real_time = true;
    testDeliversAndStops();
    testApplyDuringInflightRequestDoesNotResend();
    testNoUplinkNoRequests();
    std::cout << "upload manager lifecycle tests passed\n";
    return 0;
}
