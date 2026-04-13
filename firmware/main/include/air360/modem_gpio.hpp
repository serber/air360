#pragma once

#include <cstdint>

namespace air360 {

struct CellularConfig;  // forward declaration

// Configures PWRKEY, SLEEP, and RESET GPIO pins as push-pull outputs (idle LOW).
// Pins set to 0xFF in config are skipped silently.
void initModemGpios(const CellularConfig& config);

// Assert the PWRKEY line HIGH for duration_ms, then release it LOW.
// Polarity assumes the GPIO drives an NPN transistor that pulls PWRKEY LOW
// on the module side (active-LOW on the SIM7600E pin).
// Recommended durations: power-on ≥2 s, power-off / reset ≥3.5 s.
// No-op if gpio_pin is 0xFF.
void pulseModemPwrkey(std::uint8_t gpio_pin, std::uint32_t duration_ms);

// Drive the SLEEP/DTR line: HIGH = sleep, LOW = wake.
// No-op if gpio_pin is 0xFF.
void setModemSleepPin(std::uint8_t gpio_pin, bool assert_sleep);

}  // namespace air360
