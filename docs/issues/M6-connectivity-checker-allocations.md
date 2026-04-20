# M6 — `ConnectivityChecker` allocates an event group per call

- **Severity:** Medium
- **Area:** Performance / heap health
- **Files:**
  - `firmware/main/src/connectivity_checker.cpp`

## What is wrong

Each invocation of `ConnectivityChecker` creates a fresh FreeRTOS event group (dynamic allocation), uses it, and deletes it on exit.

`ConnectivityChecker` is called on reconnect attempts and potentially from the link-probe path (see C3). The event group is small but the allocation pattern is exactly the kind of small, frequent, variable-lifetime malloc that fragments the heap.

## Why it matters

- Heap fragmentation over time.
- Minor latency cost on every call.

## Consequences on real hardware

- Contributes to the long-term fragmentation picture described in H7.
- No immediate failure.

## Fix plan

1. **Allocate the event group once** in `ConnectivityChecker::init()` (or the constructor).
2. **Clear bits at the start of each `checkHost()` call** instead of creating/destroying.
3. **Prefer a static allocation:**
   ```cpp
   StaticEventGroup_t g_buf_;
   EventGroupHandle_t group_ = xEventGroupCreateStatic(&g_buf_);
   ```
4. **Audit the rest of `connectivity_checker.cpp`** for similar per-call allocations (e.g. timer creation, semaphore creation).
5. **Apply the same pattern** everywhere else in `main/` where a primitive is created-and-destroyed within a function.

## Verification

- Heap high-water mark after 1 000 connectivity checks within 10 % of pre-check value.
- Code grep: no `xEventGroupCreate(` inside functions that run more than once.

## Related

- H7 — part of the same STL-and-dynamic-allocation cleanup.
