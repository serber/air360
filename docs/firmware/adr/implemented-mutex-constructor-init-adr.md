# Mutex Constructor Initialization ADR

## Status

Implemented.

## Decision Summary

Replace the lazy `ensureMutex()` pattern in `MeasurementStore`, `SensorManager`, and `UploadManager` with mutex initialization in constructors, before any FreeRTOS tasks are started.

## Context

All three classes currently initialize their FreeRTOS mutex on first use:

```cpp
void MeasurementStore::ensureMutex() const {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
    }
}
```

This is called at the start of every public method from a `const` context with no synchronization. The sensor task starts at boot step 5 and begins calling `recordMeasurement()` immediately. The upload task starts at step 8 and begins calling `beginUploadWindow()`. Between those two points the mutex is already created — but the pattern is fragile: if two tasks ever called a mutex-protected method simultaneously before first use, both would see `mutex_ == nullptr` and both would attempt creation, leaking one handle.

Beyond the race, the pattern scatters initialization across runtime rather than making it explicit.

## Goals

- Initialize all mutexes before any task that uses them starts.
- Remove the `ensureMutex()` call from every public method.
- Make the initialization order explicit and verifiable by code inspection.

## Non-Goals

- Changing the FreeRTOS mutex type (`StaticSemaphore_t` stays).
- Introducing C++ `std::mutex` or RAII wrappers over FreeRTOS primitives (separate concern).

## Architectural Decision

Initialize the mutex inside the constructor of each affected class using `xSemaphoreCreateMutexStatic`. Since all affected objects are `static` locals in `App::run()` and are constructed before `applyConfig()` / `start()` are called (which spawn tasks), the mutex is guaranteed to exist before any task accesses it.

```cpp
MeasurementStore::MeasurementStore() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_buffer_);
}
```

Remove the `ensureMutex()` method and its call sites from all three classes.

## Affected Files

- `firmware/main/src/uploads/measurement_store.cpp` — remove `ensureMutex()`, init in constructor
- `firmware/main/include/air360/uploads/measurement_store.hpp` — remove `ensureMutex()` declaration
- `firmware/main/src/sensors/sensor_manager.cpp` — same
- `firmware/main/src/uploads/upload_manager.cpp` — same

## Alternatives Considered

### Option A. Keep lazy init, add atomic guard

Use `std::atomic<bool>` or a secondary pre-initialization mutex. Adds complexity for zero practical benefit given the static lifetime model.

### Option B. Constructor initialization (accepted)

Simple, explicit, matches the actual object lifetime. No runtime overhead.

## Practical Conclusion

Initialize mutexes in constructors. Remove `ensureMutex()` entirely. The change is localized to three classes and carries no risk of behavioral regression.
