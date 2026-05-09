#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define private public
#include "air360/uploads/adapters/sensor_community_uploader.hpp"
#undef private

#include "air360/string_utils.hpp"
#include "air360/uploads/upload_transport.hpp"

namespace air360 {

UploadTransportResponse UploadTransport::execute(const UploadRequestSpec& /*request*/) const {
    return {};
}

}  // namespace air360

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

const std::string* headerValue(
    const std::vector<air360::UploadRequestHeader>& headers,
    const char* name) {
    for (const auto& header : headers) {
        if (header.name == name) {
            return &header.value;
        }
    }
    return nullptr;
}

air360::BackendRecord makeSensorCommunityRecord() {
    air360::BackendRecord record{};
    record.backend_type = air360::BackendType::kSensorCommunity;
    air360::copyString(record.host, sizeof(record.host), "api.sensor.community");
    air360::copyString(record.path, sizeof(record.path), "/v1/push-sensor-data/");
    record.port = 443U;
    record.protocol = air360::BackendProtocol::kHttps;
    return record;
}

air360::MeasurementBatch makeBatch() {
    air360::MeasurementBatch batch{};
    batch.project_version = "v0.1-test";
    batch.device_id = "123456789012";
    batch.short_device_id = "789012";
    batch.esp_mac_id = "aabbccddeeff";
    batch.points.push_back(air360::MeasurementPoint{
        7U,
        air360::SensorType::kBme280,
        air360::SensorValueKind::kTemperatureC,
        24.1F,
        1744400000000ULL,
    });
    return batch;
}

void testSensorCommunitySendsShortDeviceIdAsXSensor() {
    air360::SensorCommunityUploader uploader;
    const air360::BackendRecord record = makeSensorCommunityRecord();
    const air360::MeasurementBatch batch = makeBatch();

    std::vector<air360::UploadRequestSpec> requests;
    std::string error;
    require(
        uploader.buildRequests(record, batch, requests, error),
        "buildRequests should succeed");
    require(requests.size() == 1U, "one request should be built for one supported sensor group");

    const auto& request = requests.front();
    const std::string* x_sensor = headerValue(request.headers, "X-Sensor");
    const std::string* x_mac = headerValue(request.headers, "X-MAC-ID");
    const std::string* x_pin = headerValue(request.headers, "X-PIN");
    const std::string* user_agent = headerValue(request.headers, "User-Agent");

    require(x_sensor != nullptr, "X-Sensor header should be present");
    require(*x_sensor == "esp32-789012", "X-Sensor should use esp32-{short_device_id}");
    require(x_mac != nullptr && *x_mac == "esp32-aabbccddeeff", "X-MAC-ID should include ESP MAC");
    require(x_pin != nullptr && *x_pin == "11", "BME280 should use Sensor.Community pin 11");
    require(
        user_agent != nullptr && *user_agent == "v0.1-test/789012/aabbccddeeff",
        "User-Agent should include the same Sensor.Community device id");
}

void testSensorCommunityFallsBackToFullDeviceId() {
    air360::SensorCommunityUploader uploader;
    const air360::BackendRecord record = makeSensorCommunityRecord();
    air360::MeasurementBatch batch = makeBatch();
    batch.short_device_id.clear();

    std::vector<air360::UploadRequestSpec> requests;
    std::string error;
    require(
        uploader.buildRequests(record, batch, requests, error),
        "buildRequests should succeed without short_device_id");
    require(requests.size() == 1U, "one request should be built");

    const std::string* x_sensor = headerValue(requests.front().headers, "X-Sensor");
    require(x_sensor != nullptr, "X-Sensor header should be present");
    require(*x_sensor == "esp32-123456789012", "X-Sensor should fallback to full device_id");
}

}  // namespace

int main() {
    testSensorCommunitySendsShortDeviceIdAsXSensor();
    testSensorCommunityFallsBackToFullDeviceId();
    std::cout << "sensor_community_uploader tests passed\n";
    return 0;
}
