# H8 — Cellular 5-attempt PWRKEY cadence is wear-prone

- **Severity:** High
- **Area:** Cellular reliability / hardware longevity
- **Files:**
  - `firmware/main/src/cellular_manager.cpp` (reconnect loop)
  - `firmware/main/src/modem_gpio.cpp` (`pulseModemPwrkey`)

## What is wrong

The cellular reconnect loop counts attempts. After 5 failed attempts, the code hard-resets the modem by pulsing PWRKEY. The threshold is attempt-count, not elapsed time.

## Why it matters

- Under a carrier outage or denied registration (wrong APN, roaming policy, weak signal), attempts happen rapidly.
- 5 attempts at ~10 s each = 50 s until PWRKEY. On a signal-weak window, the device may PWRKEY-cycle every minute.
- PWRKEY cycling:
  - Stresses the PWRKEY pin and the modem's internal power sequencer.
  - Drains power — meaningful on battery-backed deployments.
  - Takes ~10–20 s per cycle, during which no uplink is possible.
  - Carriers can rate-limit or blacklist devices that re-register aggressively.

## Consequences on real hardware

- Devices in marginal coverage (rural, basement) burn modem lifetime faster than devices in good coverage.
- Carriers may throttle or deprioritize a chronically-re-registering IMEI.
- Field reports of "modem died after 18 months" when the spec is 7+ years.

## Fix plan

1. **Switch to time-based escalation with exponential backoff.**
   - Track elapsed time in the current failure window, not attempts.
   - Backoff table: 10 s, 30 s, 1 min, 2 min, 5 min, 10 min, 15 min (cap).
   - Only PWRKEY-reset after N minutes of continuous failure (e.g. 10 min) — not after N attempts.
2. **Separate "soft" and "hard" retries.**
   - Soft retry: retry the current modem state (redial PPP). Cheap.
   - Hard retry: command-mode → data-mode cycle. Medium.
   - PWRKEY: nuclear. Expensive. Rare.
3. **Cap PWRKEY frequency.** At most one PWRKEY cycle per hour under normal operation. If the modem needs more than that, escalate to a full system reboot after the second PWRKEY — that at least clears any ESP-side state accumulation.
4. **Carrier-specific backoff.** Read `AT+CEREG?` / `AT+CREG?` — if the modem reports "not registered, searching," keep doing nothing and keep polling; don't treat "searching" as a failure.
5. **Log the escalation ladder** clearly: one ERROR line per transition (soft → hard → PWRKEY → reboot). Fleet ops can alarm on PWRKEY frequency.
6. **Expose counters** on the status JSON: `cellular.pwrkey_cycles_total`, `cellular.last_pwrkey_ms_ago`, `cellular.consecutive_failures`.

## Verification

- Simulated outage: disconnect antenna for 60 min; verify ≤ 1 PWRKEY cycle in that window.
- Soak on marginal signal: 24 h with antenna detuned; PWRKEY count ≤ 2.
- Normal network: PWRKEY count stays 0 forever.

## Related

- C3 — bounded wait for `kLostIpBit` is a prerequisite; without it, escalation cannot even start.
- C4 — cellular task must be on TWDT during all wait phases.
