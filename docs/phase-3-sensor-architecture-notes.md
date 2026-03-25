# Phase 3 Sensor Architecture Notes

## Scope

This document describes the proposed implementation of the next major block after Phase 2: user-configured sensors, web-based sensor setup, persisted sensor configuration, and sensor runtime startup.

This note refers to that work as `Phase 3`: the first sensor-oriented phase after Phase 2.

## Core Design Goal

The sensor abstraction should be designed so that adding support for a new sensor is as simple as possible.

In practical terms, adding a new sensor should ideally require only:

- one new driver implementation
- one registry entry describing that driver
- optional driver-specific field definitions if the sensor needs extra configuration

It should not require touching unrelated runtime code such as:

- `WebServer`
- `StatusService`
- `App`
- generic polling orchestration
- generic persistence plumbing

If the architecture forces changes across multiple unrelated modules every time a new sensor is added, the abstraction is too weak.

## What Should Be Implemented

Phase 3 should be split into two stages.

### Phase 3.1

- Add and edit a list of sensors through the web UI.
- Persist sensor configuration in storage.
- Load the configured sensor list at boot.
- Build sensor runtime state from the stored configuration.
- Initialize transport-level resources and prepare the polling loop.
- Expose configured sensors and their state through `/` and `/status`.

At this stage, the firmware does not need full sensor reading support for every sensor type yet. The main outcome is that the device understands which sensors are configured, where they are attached, and how to construct runtime instances for them at boot.

### Phase 3.2

- Add the first real sensor implementation for BME over I2C.
- Initialize a BME sensor instance from stored configuration.
- Poll it periodically.
- Publish the latest readings and runtime status.
- Handle a missing sensor, invalid address, and read failures.

In other words:

- Phase 3.1 introduces the architecture and persistence model.
- Phase 3.2 introduces the first working sensor driver.

## Why This Should Be Separate From the Current Config Page

The current `/config` flow in [`firmware/main/src/web_server.cpp`](../firmware/main/src/web_server.cpp) already covers device onboarding:

- device name
- station Wi-Fi
- setup AP
- local auth placeholder

Sensor configuration has a different shape:

- it is a list of records, not a single fixed form
- it includes transport-specific fields
- its lifecycle is closer to runtime modules than onboarding
- it will grow over time as more drivers and attachment types are added

Because of that, the current `/config` page should not be overloaded. A separate `/sensors` page is the cleaner design:

- `GET /sensors` shows the configured sensor list and the add/edit form
- `POST /sensors` creates a new record
- `POST /sensors/<id>` updates an existing record
- `POST /sensors/<id>/delete` removes a record

`/config` remains focused on network and device identity. `/sensors` becomes the sensor inventory UI.

## Recommended Architecture

### 1. Separate Sensor Config From Device Config

The current [`DeviceConfig`](../firmware/main/include/air360/config_repository.hpp) is stored as a single fixed blob in NVS. That works well for small scalar configuration, but it is a poor fit for a sensor list:

- the list is variable-length
- different sensors need different transport fields
- new sensor types and binding options will appear over time
- the sensor schema will likely change more often than the onboarding schema

Because of that, sensor configuration should be stored separately from `DeviceConfig`.

Recommended model:

- keep `DeviceConfig` for network and device settings
- add a dedicated `SensorConfigRepository`
- store the sensor list as a separate blob key or as multiple records in a dedicated NVS area

The minimum practical version for the near term:

- namespace: `air360`
- key: `sensor_cfg`
- payload: one serialized sensor-record list with an explicit `schema_version`

That is the simplest way to start. If the list grows later, the implementation can move to per-sensor records without changing the UI model first.

### 2. Split Driver Logic From Transport Binding

The main architectural decision is this: sensors should not be split at the top level into two unrelated worlds such as "all I2C sensors inherit from one base" and "all analog sensors inherit from another base". The cleaner split is:

- sensor driver: logic for the specific sensor
- transport binding: where and how that sensor is attached

That means the upper layer should stay common:

- `SensorInstance`
- `SensorDriver`
- `SensorManager`

Transport-specific differences should move into binding and config objects:

- `I2cBinding`
- `AnalogBinding`

Why this is better:

- the lifecycle is the same for every sensor: configure, init, poll, report status
- UI and persistence operate on sensor instances, not low-level transport APIs
- a sensor family may eventually support more than one connection model
- runtime orchestration does not need to be duplicated between an `I2cSensorBase` and an `AnalogSensorBase`

Transport-specific helper classes are still useful, but they should live below the main sensor API.

### 3. Runtime Modules

Recommended module split:

- `SensorRegistry`
  - list of supported sensor types
  - display names for the UI
  - supported transport kinds for each type
  - sensor-specific config field metadata for the UI
  - factory logic to create drivers
- `SensorConfigRepository`
  - load and save the sensor list
  - schema versioning
  - validation of serialized config
- `SensorManager`
  - reads the configured sensor list
  - creates runtime instances
  - initializes bindings
  - starts polling
  - stores last status and last measurement
- `SensorDriver`
  - common interface for each concrete sensor
  - the BME implementation is the first concrete driver
- `TransportBindings`
  - `I2cBinding`
  - `AnalogBinding`
  - later, possibly `UartBinding`, `OneWireBinding`, and others

The important rule here is that the registry should carry enough metadata for the rest of the system to stay generic.

That means:

- the web UI should build its sensor-type choices from `SensorRegistry`
- transport validation should use registry-declared capabilities
- runtime construction should use registry factories
- status output should work from generic sensor runtime state, not sensor-specific hardcoded branches

With that model, a newly added driver becomes mostly a data and factory registration problem rather than a full-stack refactor.

### 4. Suggested File Layout

The sensor subsystem should move into dedicated files instead of continuing to grow inline inside `app.cpp` and `web_server.cpp`.

Suggested layout:

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/include/air360/sensors/sensor_config.hpp`
- `firmware/main/include/air360/sensors/sensor_manager.hpp`
- `firmware/main/include/air360/sensors/sensor_registry.hpp`
- `firmware/main/include/air360/sensors/sensor_driver.hpp`
- `firmware/main/include/air360/sensors/transport_binding.hpp`
- `firmware/main/include/air360/sensors/drivers/bme280_sensor.hpp`
- `firmware/main/src/sensors/sensor_manager.cpp`
- `firmware/main/src/sensors/sensor_registry.cpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/sensors/transport_binding.cpp`
- `firmware/main/src/sensors/drivers/bme280_sensor.cpp`

That structure is a better fit for a growing driver set than continuing to place everything inside `ConfigRepository` and `WebServer`.

## Extensibility Rules

To keep new sensor support cheap to add, the subsystem should follow a few strict rules.

### 1. Registry-Driven, Not Hardcoded in the UI

The `/sensors` page should not contain hardcoded knowledge of specific sensor types such as BME.

Instead, it should ask `SensorRegistry` for:

- supported sensor types
- display labels
- supported transport kinds
- any driver-specific config fields

This keeps the web layer generic.

### 2. Generic Persistence Format

Persistence should store:

- generic sensor record fields
- transport binding fields
- optional driver-specific config payload

That payload should be versioned and validated by the driver or registry descriptor, not by scattered logic in unrelated modules.

### 3. Generic Runtime Lifecycle

Every driver should fit the same lifecycle:

- construct
- configure
- init
- poll
- expose status
- expose latest measurement set

That allows `SensorManager` to remain unchanged when new drivers appear.

### 4. Driver-Specific Details Stay Inside the Driver

A concrete driver should own:

- low-level protocol logic
- sensor-specific init sequence
- sensor-specific measurement parsing
- validation of sensor-specific config fields

It should not force sensor-specific branches into `WebServer`, `StatusService`, or the boot flow.

### 5. Transport Logic Stays Below the Driver Contract

Drivers should declare which transports they support, but the generic system should own transport construction and validation.

That prevents every new I2C or analog driver from reinventing the same binding logic.

## What Adding a New Sensor Should Look Like

The target developer experience should be close to this:

1. Add a new driver file, for example `sht31_sensor.cpp/.hpp`.
2. Implement the common `SensorDriver` interface.
3. Register the sensor in `SensorRegistry` with:
   - type id
   - display name
   - supported transport kinds
   - optional config field schema
   - factory callback
4. Optionally add sensor-specific validation tests.
5. Rebuild.

That should be enough for the new sensor to:

- appear in the `/sensors` UI
- be accepted by config validation
- be persisted in storage
- be instantiated at boot
- expose status through the generic runtime path

If additional edits are required in unrelated core modules, that should be treated as an architecture smell and a sign that the abstraction needs to be tightened.

## Configuration Model

### Sensor Record

Each configured sensor should be represented as a separate logical record.

Suggested minimum shape:

- `id`
- `enabled`
- `sensor_type`
- `display_name`
- `transport_kind`
- `poll_interval_ms`
- `binding`

Where:

- `id` is the internal stable record identifier
- `display_name` is a user-facing label
- `sensor_type` is a registry type such as `bme280`
- `transport_kind` is `i2c` or `analog`
- `binding` is the transport-specific payload

### I2C Binding

For I2C sensors, the record should include:

- `bus_id`
- `address`

If board profiles with named attachment points appear later, `bus_id` can stay logical:

- `i2c0`
- `i2c1`
- or preferably `env_i2c`, `aux_i2c`

For the first version, a reasonable compromise is:

- the UI shows a clear list of available I2C ports
- the config stores a short stable identifier
- actual SDA and SCL pins remain the responsibility of the board profile or the firmware wiring layer

The first version should not force the user to type raw `SDA` and `SCL` pins if the board already implies fixed I2C attachment points.

### Analog Binding

For analog sensors, the record should include:

- `gpio_pin`

Possible later extensions:

- `adc_unit`
- `attenuation`
- `sample_count`

For the first version, `gpio_pin` is enough, but the save path should validate:

- that the selected pin is valid for ADC on the current board
- that it does not conflict with known reserved board pins

So a raw pin number can remain a user-facing field, but validation should be centralized.

## Sensor Driver Interface

The top-level driver interface should stay uniform across sensor types. At a high level:

- `describe()`
- `configure(binding, config)`
- `init()`
- `poll()`
- `currentStatus()`
- `lastMeasurement()`

Transport-specific helpers can live below that interface.

In practice, that means:

- `Bme280Sensor` implements the common `SensorDriver`
- internally it uses `I2cBinding`
- future analog drivers use the same runtime contract with a different binding object

That approach reduces coupling between the web UI, persistence, and the transport layer.

If a sensor needs driver-specific parameters beyond the generic fields, they should be represented as a small driver-owned config block described through the registry rather than as ad hoc fields added directly to the top-level web page logic.

## Web UI Flow

### Proposed Page

A dedicated `/sensors` page.

It should include:

- a list of configured sensors
- an `Add sensor` action
- a form for creating or editing a record

### Add Sensor Flow

The user flow should look like this:

1. Open `/sensors`
2. Click `Add sensor`
3. Choose a sensor type from a list
4. Choose a transport kind from the options supported by that sensor
5. If `i2c` is selected, choose the I2C port and, if needed, the address
6. If `analog` is selected, enter the pin number
7. Optionally set a display name and polling interval
8. Save

After saving, the record is immediately persisted.

The important implementation detail is that the form should be generated from generic sensor metadata plus registry-provided field definitions, not from a growing list of hardcoded `if sensor_type == ...` branches.

### UI Behavior Rules

- If a sensor type supports only I2C, the form should not offer `analog`.
- If a sensor type supports only analog, the form should not offer I2C fields.
- Validation errors should return to the same page.
- The sensor list should show runtime state:
  - `configured`
  - `initialized`
  - `polling`
  - `absent`
  - `error`

## Boot Flow Changes

The current startup path already goes through `ConfigRepository`, `NetworkManager`, `WebServer`, and `StatusService`. The sensor subsystem should be inserted after device config is loaded and before the full status and UI loop is considered ready.

Suggested flow:

1. Load `DeviceConfig` as today.
2. Load `SensorConfigList` from `SensorConfigRepository`.
3. Validate the list and drop invalid records with explicit logging.
4. Initialize `SensorManager`.
5. For each enabled record:
   - find the descriptor in `SensorRegistry`
   - create the driver instance
   - create the transport binding
   - call `init()`
6. Start a periodic polling task or timer-driven polling loop.
7. Expose runtime state through `StatusService` and `WebServer`.

At the first stage, it is acceptable for sensors without a working driver yet to enter a state such as `configured` or `unsupported`, as long as they do not break the entire boot sequence.

## BME Implementation

The first concrete implementation should be a BME sensor over I2C.

Phase 3.2 for BME should include:

- sensor type `bme280`
- supported transport: only `i2c`
- configurable I2C address
- init sequence
- periodic reads for:
  - temperature
  - humidity
  - pressure
- status transitions:
  - `configured`
  - `initialized`
  - `polling`
  - `absent`
  - `error`

It is important that a missing BME on the configured address does not fail the whole boot. The corresponding record should remain visible as `absent` with a clear diagnostic in `/status`.

## Validation Rules

Validation should be split into two layers.

### Config-Time Validation

- known `sensor_type`
- supported `transport_kind` for that sensor
- valid `bus_id` or `gpio_pin`
- valid I2C address
- reasonable `poll_interval_ms`

### Runtime Validation

- the device responds during init
- the binding can be opened
- reads return valid data

That distinction matters because a valid form submission still does not guarantee that the physical sensor is actually present.

## Data That Should Appear in Status Output

For each sensor, `/status` should include:

- `id`
- `sensor_type`
- `display_name`
- `transport_kind`
- `binding`
- `enabled`
- `status`
- `last_error`
- `last_sample_time`
- the latest measurements, if available

The HTML root and status summary can stay shorter:

- name
- type
- port
- state
- latest readings

## Recommended Implementation Order

### Step 1

- introduce `SensorConfig` and `SensorConfigRepository`
- extend storage with a separate sensor blob
- add `/sensors` to `WebServer`
- add a sensor summary to `StatusService`
- load the sensor list during startup

### Step 2

- introduce `SensorRegistry`
- introduce `SensorManager`
- construct runtime instances from the persisted list
- log init results

### Step 3

- implement `Bme280Sensor`
- add the polling task
- expose real readings through status and UI

## Recommendation on I2C Versus Analog Abstraction

Short answer: yes, the split is needed, but not as two disconnected top-level architectures.

The cleaner model is:

- one shared sensor lifecycle interface
- transport-specific binding objects below it
- the registry knows which transports each driver supports

So, in the UI, the user still chooses `I2C` or `Analog`, but inside the firmware:

- orchestration is unified
- persistence is unified
- the transport layer differs

That gives the cleanest path for additional sensor types and does not overfit the architecture to the initial BME case.

## Practical Recommendation

For the next implementation slice, this is the recommended approach:

1. Do not extend the current `DeviceConfig` with a sensor list.
2. Add a dedicated sensor config repository.
3. Do not mix sensor UI into `/config`; add `/sensors`.
4. Use one common `SensorDriver` interface.
5. Move `I2cBinding` and `AnalogBinding` into a dedicated lower layer.
6. Implement BME as the first I2C-only driver.
7. Expose configured sensors and runtime state through `/status` from the start.
8. Treat "adding a new sensor in one driver plus one registry entry" as an explicit architecture requirement.

This provides a good foundation for the next phase without overengineering and without forcing the project into a dead end before more sensor types appear.
