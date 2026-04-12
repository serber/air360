# DHT11 / DHT22

Digital temperature and humidity sensors with a single-wire interface. One driver handles both variants — the model is selected at creation time via the sensor registry.

## Transport

- GPIO (single-wire digital protocol)
- Pin configured from the sensor record (`record.analog_gpio_pin`)
- Valid pins: GPIO4, GPIO5, GPIO6 (configurable via `Kconfig`)

## Initialization

1. Verify that `analog_gpio_pin >= 0`
2. Set `initialized_` to `true`

No hardware GPIO initialization takes place — all pin interaction happens on each poll through the `dht` library.

## Polling

Each poll cycle:
1. Map the driver model to library type: `DHT_TYPE_DHT11` or `DHT_TYPE_AM2301` (DHT22)
2. Read temperature and humidity via `dht_read_float_data(type, gpio_pin, &humidity, &temperature)`
3. Validate that no NaN values are returned

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Temperature | `kTemperatureC` | °C |
| Humidity | `kHumidityPercent` | % |

## DHT11 vs DHT22

| Characteristic | DHT11 | DHT22 |
|----------------|-------|-------|
| Temperature range | 0…50 °C | −40…80 °C |
| Temperature accuracy | ±2 °C | ±0.5 °C |
| Humidity range | 20…90 % | 0…100 % |
| Humidity accuracy | ±5 % | ±2–5 % |
| Minimum poll interval | 2 s | 2 s |

## Notes

- The read is blocking — `dht_read_float_data()` holds the bus for the duration of the DHT protocol (tens of milliseconds). This executes inside the sensor task.
- DHT22 is identified internally as `DHT_TYPE_AM2301` (compatible model designation).
- If the read fails or returns NaN, `initialized_` is reset. Re-initialization is trivial (just a pin check) so recovery is immediate on the next cycle.
- The sensor requires a 10 kΩ pull-up resistor to VCC on the data line.

## Recommended poll interval

Minimum 2 seconds (protocol limit). 5 seconds or more is recommended for stable operation.

## Component

`esp-idf-lib__dht` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/dht_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/dht_sensor.hpp`
