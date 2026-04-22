# H10 — Cross-backend prune cursor lacks property tests

- **Severity:** High
- **Area:** Upload correctness / testability
- **Status:** Implemented
- **Files:**
  - `firmware/main/src/uploads/upload_manager.cpp`
  - `firmware/main/src/uploads/upload_manager_config.cpp`
  - `firmware/main/src/uploads/upload_manager_status.cpp`
  - `firmware/main/src/uploads/upload_prune_policy.cpp`
  - `firmware/main/src/uploads/measurement_store.cpp`

## What is wrong

`UploadManager` maintains per-backend inflight retry windows and a cross-backend prune cursor. The store advances the "acknowledged-up-to" cursor only when *all* backends have acknowledged past a given sequence number.

There are very few tests exercising this logic. It is notoriously easy to get wrong:

- A permanently broken backend stalls pruning forever.
- A flapping backend can race with a healthy one and cause double-acknowledge.
- Boundary cases at wrap-around or empty-queue moments are untested.

## Why it matters

- Silently dropping a sample because the cursor advanced prematurely is worse than any visible failure.
- Silently stalling pruning wastes queue capacity and eventually causes drops (linked to C2).
- Without property-based tests, every change to this code is a gamble.

## Consequences on real hardware

- A customer uses a known-bad custom backend for testing, the device queue fills and starts dropping — while other backends remain healthy.
- A backend migration mid-run causes one sample to be delivered twice or zero times.

## Fix plan

1. **Extract the logic into a testable unit.** `MeasurementStore::prune(const PerBackendCursor& cursors)` should be a pure function testable without ESP-IDF.
2. **Define invariants.** Write them down in comments and enforce in tests:
   - INVARIANT 1: A sample is pruned only when all active backends have acknowledged it.
   - INVARIANT 2: A disabled backend does not block pruning (it is removed from the quorum).
   - INVARIANT 3: A permanently failing backend, after M consecutive failures over T minutes, is demoted to "best-effort" and removed from the quorum. A separate counter tracks samples it missed.
   - INVARIANT 4: No sample is ever acknowledged twice.
   - INVARIANT 5: The cursor is monotonic.
3. **Write host-side unit tests** under `firmware/test/` covering:
   - Single backend happy path.
   - Two backends, one permanently failing.
   - Two backends, one flapping (random success/failure).
   - Backend added or removed at runtime.
   - Queue full + backlog flush.
4. **Add property tests** (can be hand-rolled in C++ — randomized sequences of {enqueue, ack, disable, enable, fail}). Assert invariants after every step.
5. **Split `upload_manager.cpp`** while you're here. 705 lines in one file is a maintenance issue on its own.
6. **Wire the tests into CI** (if CI exists for the firmware; if not, document the host-build target and make it part of `check_firmware_docs.py` or a sibling script).

## Verification

- `upload_manager` tests pass and cover all five invariants.
- A deliberate regression (swap two lines in `prune`) is caught by the tests.
- Code coverage on `measurement_store.cpp` > 80 %.

## Related

- C2 — persistent queue's cursor must obey the same invariants.
- H6 — retry/backoff logic in sensor init is a separate concern but benefits from the same testing discipline.
