#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

class GpsNmeaSensor final : public SensorDriver {
  public:
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t processBytes(const std::uint8_t* data, std::size_t size, bool& saw_valid_fix);
    bool processSentence(const std::string& sentence, bool& saw_valid_fix);
    bool processGga(const std::string& payload, bool& saw_valid_fix);
    bool processRmc(const std::string& payload, bool& saw_valid_fix);
    bool validateChecksum(const std::string& sentence) const;
    static bool parseCoordinate(
        const std::string& raw_value,
        const std::string& hemisphere,
        bool latitude,
        float& out_value);
    static bool parseFloat(const std::string& input, float& out_value);
    static bool parseUnsigned(const std::string& input, unsigned long& out_value);
    void rebuildMeasurement(bool has_fix);
    void setError(const std::string& message);

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::string line_buffer_;
    bool initialized_ = false;
    bool has_latitude_ = false;
    bool has_longitude_ = false;
    bool has_altitude_ = false;
    bool has_satellites_ = false;
    bool has_speed_knots_ = false;
    float latitude_deg_ = 0.0F;
    float longitude_deg_ = 0.0F;
    float altitude_m_ = 0.0F;
    float satellites_ = 0.0F;
    float speed_knots_ = 0.0F;
};

std::unique_ptr<SensorDriver> createGpsNmeaSensor();

}  // namespace air360
