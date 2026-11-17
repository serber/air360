#include "air360/sensors/sensor_driver.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Mirrors SensorManager's record step: count how many polls would be handed to
// the MeasurementStore.
struct Recorder {
    std::uint64_t last_recorded_sample_time_ms = 0U;
    int recorded = 0;

    void onPoll(const air360::SensorMeasurement& measurement) {
        if (air360::isFreshSample(measurement, last_recorded_sample_time_ms)) {
            last_recorded_sample_time_ms = measurement.sample_time_ms;
            ++recorded;
        }
    }
};

air360::SensorMeasurement frame(std::uint64_t sample_time_ms, float pm25) {
    air360::SensorMeasurement measurement;
    measurement.sample_time_ms = sample_time_ms;
    measurement.addValue(air360::SensorValueKind::kPm2_5UgM3, pm25);
    return measurement;
}

}  // namespace

int main() {
    // One valid frame followed by polls that return the cached measurement
    // (empty or corrupt UART reads) produces exactly one observation.
    Recorder recorder;
    const air360::SensorMeasurement first = frame(1000U, 12.0F);
    recorder.onPoll(first);
    for (int poll = 0; poll < 10; ++poll) {
        recorder.onPoll(first);
    }
    require(recorder.recorded == 1, "cached frame must be recorded once");

    // A fresh frame resumes normal recording, even with an identical value.
    recorder.onPoll(frame(6000U, 12.0F));
    require(recorder.recorded == 2, "fresh frame must be recorded");
    recorder.onPoll(frame(11000U, 13.5F));
    require(recorder.recorded == 3, "next fresh frame must be recorded");

    // Empty measurements and measurements without a sample time never count.
    recorder.onPoll(air360::SensorMeasurement{});
    air360::SensorMeasurement unstamped = frame(0U, 1.0F);
    recorder.onPoll(unstamped);
    require(recorder.recorded == 3, "empty or unstamped samples must be ignored");

    std::cout << "sensor sample freshness tests passed\n";
    return 0;
}
