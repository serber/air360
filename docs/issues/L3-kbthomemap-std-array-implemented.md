# L3 — Raw array + `sizeof`/`sizeof[0]` instead of `std::array`

- **Severity:** Low
- **Area:** Modern C++ hygiene
- **Files:**
  - `firmware/main/src/ble_advertiser.cpp` (`constexpr BthomeEntry kBthomeMap[] = {...}; constexpr std::size_t kBthomeMapSize = sizeof(kBthomeMap) / sizeof(kBthomeMap[0]);`)
  - Likely similar patterns in other files — audit with a grep.

## What is wrong

Compile-time arrays are declared as C-style arrays with a manual size constant computed via `sizeof`. This is a C++03-era idiom; `std::array` provides `.size()` as a member.

## Why it matters

- Readability and consistency with modern C++.
- `std::array` also interoperates with `<algorithm>` without decay.
- Accidentally taking a pointer and losing the compile-time size is harder with `std::array`.

## Consequences on real hardware

- None.

## Fix plan

1. **Replace the array declaration:**
   ```cpp
   constexpr std::array<BthomeEntry, 7> kBthomeMap = {{
       {SensorValueKind::kTemperatureC,    0x02U, 2U, true,  100.0f},
       // ...
   }};
   ```
2. **Replace usages of `kBthomeMapSize`** with `kBthomeMap.size()`. The constant can be removed if no longer needed, or kept as:
   ```cpp
   constexpr std::size_t kBthomeMapSize = kBthomeMap.size();
   ```
3. **Sweep the codebase** for similar `sizeof(x)/sizeof(x[0])` idioms.

## Verification

- `grep -rn "sizeof.*/ sizeof" firmware/main/` returns nothing or only cases that genuinely need it.

## Related

- L2 — part of the same `ble_advertiser` cleanup.

## Implemented

Implemented.

- `kBthomeMap` in `firmware/main/src/ble_advertiser.cpp` now uses `std::array` and `.size()`.
- The same `sizeof(arr) / sizeof(arr[0])` idiom was removed from the other remaining `firmware/main/` call sites:
  - `firmware/main/src/web_server.cpp`
  - `firmware/main/src/cellular_manager.cpp`
  - `firmware/main/src/uploads/backend_registry.cpp`
  - `firmware/main/src/sensors/sensor_registry.cpp`
- `rg -n "sizeof\\s*\\([^\\)]*\\)\\s*/\\s*sizeof\\s*\\([^\\)]*\\[[^\\]]+\\]\\)" firmware/main` returns no matches.
