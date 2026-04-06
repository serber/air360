# Measurement Runtime Separation ADR

## Status

Proposed.

This document records a planned architecture decision for the firmware. It is not a description of already implemented behavior.

## Decision Summary

Air360 should separate sensor lifecycle management from measurement storage and upload queueing.

The intended target shape is:

- `SensorManager` manages drivers, polling, and sensor runtime state
- a separate measurement runtime component owns:
  - the global sample queue
  - the in-flight upload window
  - the latest measurement snapshot for each sensor
- the system keeps a single global bounded queue, not multiple queues per backend or per sensor

The queue remains fixed-size. When it fills up, the oldest queued samples are dropped.

## Context

The current firmware already has a `MeasurementStore`, but the sensor runtime still owns part of the measurement-facing state and appends directly into the upload queue from inside `SensorManager`.

That creates tighter coupling than necessary between:

- sensor lifecycle
- latest values shown in the UI
- upload queueing behavior

This coupling makes future work harder than it needs to be, especially:

- live sensor reconfiguration without reboot
- clearer queue and backlog policies
- independent evolution of UI-facing latest values versus upload buffering
- better runtime diagnostics

The goal is not to create a complex pipeline. The goal is to make the ownership boundaries explicit and stable.

## Goals

- keep `SensorManager` focused on drivers and polling
- make measurement state a first-class runtime concern of its own
- preserve a single simple global queue
- keep the queue bounded and predictable under all conditions
- keep the latest local sensor values available to the UI even when uploads are failing or disabled
- make future live sensor apply easier to implement

## Non-Goals

- introducing separate queues per backend
- introducing separate queues per sensor
- introducing durable on-flash measurement persistence in the first step
- redesigning the backend pipeline around per-destination buffering
- making buffering conditional on backend availability

## Architectural Decision

### 1. `SensorManager` should stop owning measurement payload state

`SensorManager` should remain responsible for:

- applying sensor configuration
- creating and stopping drivers
- polling drivers on schedule
- tracking runtime state such as:
  - enabled/disabled
  - initialized/error/absent state
  - binding summary
  - poll interval
  - last driver error

`SensorManager` should not be the owner of:

- the latest measurement snapshot used by the UI
- queued sample counts
- upload queue insertion policy

### 2. A dedicated measurement runtime component should own latest values and the queue

Introduce or reshape the existing measurement layer so it owns:

- `latest measurement` per `sensor_id`
- `pending` queue
- `inflight` upload window

The important point is ownership, not naming. The existing `MeasurementStore` may evolve into this role rather than introducing a completely new type.

### 3. Keep a single bounded queue

The measurement pipeline should continue to use one global queue.

Reasons:

- simpler implementation
- simpler mental model
- easier diagnostics
- no need to coordinate multiple buffers or per-backend retention policies

When the queue reaches its fixed size:

- the oldest queued samples are dropped first

This preserves bounded memory usage and predictable behavior.

### 4. Latest local values and queued upload samples are related but distinct

Each new sensor reading should be handled in two separate ways:

1. update the latest-measurement snapshot for that sensor
2. append an uploadable sample into the bounded queue

This distinction matters because:

- the UI should always show the newest local sensor values
- the upload queue is allowed to drop old samples under pressure

Therefore:

- local monitoring must continue to work even when queued upload data is being truncated
- queue policy must not erase the latest known reading shown to the user

## Expected Data Flow

Preferred runtime flow:

1. `SensorManager` polls a driver
2. the poll returns a `SensorMeasurement`
3. `SensorManager` forwards that reading into the measurement runtime
4. the measurement runtime:
   - updates the latest measurement for that sensor
   - appends a queue sample if the reading is uploadable
   - applies the fixed-size eviction policy if needed
5. `UploadManager` reads batches from the same global queue

This preserves one clear pipeline:

- sensors produce measurements
- the measurement runtime owns measurement state
- upload consumes batches

## Alternatives Considered

### Option A. Keep the current mixed ownership

Rejected as the long-term direction.

Why:

- sensor runtime and queue policy stay unnecessarily coupled
- UI-facing state and upload-facing state are harder to reason about
- live sensor apply becomes more awkward than it should be

### Option B. Separate latest values and queue ownership, keep one queue

Accepted direction.

Why:

- clean enough without overengineering
- easy to explain
- good fit for the current firmware scale
- preserves bounded memory behavior

### Option C. Multiple queues per backend or per sensor

Rejected for now.

Why:

- higher complexity
- harder to reason about fairness and retention
- no clear near-term product need
- does not match the current simplicity goals

## Queue Policy

The queue should remain:

- global
- bounded
- fixed-size

Behavior under pressure:

- if the queue is not full, append the sample
- if the queue is full, drop the oldest queued sample and append the new one

This creates a sliding-window buffer.

Important clarification:

- even if no backend is currently enabled, the latest measurement state still matters for the UI
- the queue may still exist and behave as a bounded sliding window
- queue truncation must not interfere with the latest local reading shown to the user

## Implications For Status And UI

After this separation, UI and `/status` should assemble sensor views from two sources:

- sensor runtime state from `SensorManager`
- latest measurement and queue state from the measurement runtime

Useful values include:

- latest measurement
- latest sample time
- queued sample count per sensor
- total queued samples

## Implications For Live Sensor Apply

This decision is meant to make no-reboot sensor reconfiguration easier.

Why:

- sensor driver lifecycle stays inside `SensorManager`
- measurement history and latest-value snapshots remain outside driver ownership
- removing or reconfiguring a sensor does not require the queue subsystem to be torn down

Queued samples that were already collected for a sensor may remain in the queue and be uploaded later. Removing a sensor should stop future sampling, not invalidate already collected samples.

## Recommended Refactor Direction

The preferred implementation sequence is:

1. make the measurement runtime the owner of latest values per sensor
2. route new sensor readings through that measurement runtime instead of pushing directly into queue logic from scattered call sites
3. remove measurement payload ownership from `SensorRuntimeInfo`
4. update `StatusService` and UI composition to read runtime state and measurement state separately
5. only then pursue live sensor apply without reboot

## Practical Conclusion

Air360 should move toward a cleaner split:

- `SensorManager` owns drivers and polling
- the measurement runtime owns latest values and the single bounded queue

This keeps the system simple, bounded, and easier to evolve without introducing multiple queues or unnecessary buffering complexity.
