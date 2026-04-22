# H9 — `using namespace esp_modem;` in a translation unit

- **Severity:** High (code hygiene / future ABI risk)
- **Area:** Code quality / maintainability
- **Status:** Implemented
- **Files:**
  - `firmware/main/src/cellular_manager.cpp` (top of file)

## What is wrong

The file pulls the entire `esp_modem` namespace into the translation unit with `using namespace esp_modem;`. The statement is suppressed with a NOLINT — someone noticed the lint warning and silenced it rather than fixing it.

## Why it matters

- Vendor namespaces evolve. A future `esp_modem` release can add a symbol (`class Foo`) that collides with a local name in this file. The compiler error will be remote from the cause.
- Lint suppressions accumulate: they normalize ignoring warnings, which is the start of a debt spiral.
- Makes the code harder to read — it's not obvious which types are vendor vs local.

## Consequences on real hardware

- None directly; this is a latent maintenance hazard.
- Becomes a real problem on ESP-IDF upgrades when unexpected compile errors appear.

## Fix plan

1. **Remove the `using namespace esp_modem;` line.**
2. **Add targeted `using` declarations** for each symbol actually used:
   ```cpp
   using esp_modem::DTE;
   using esp_modem::DCE;
   using esp_modem::create_uart_dte;
   // etc.
   ```
3. **Or fully qualify** the uses inline. For a file this size, targeted `using` declarations are cleaner.
4. **Remove the NOLINT.** The line should no longer need suppression.
5. **Add a lint rule** (`clang-tidy` config) that flags `using namespace` in `.cpp` files outside test utilities.

## Verification

- `grep "using namespace" firmware/main/src/` returns no results in production code.
- Build is clean.
- clang-tidy configuration in `.clang-tidy` enables `google-build-using-namespace`.

## Related

- None; isolated hygiene fix.
