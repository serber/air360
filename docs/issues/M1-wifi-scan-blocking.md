# M1 — `scanAvailableNetworks` uses blocking Wi-Fi scan

- **Severity:** Medium
- **Area:** Networking / web responsiveness
- **Files:**
  - `firmware/main/src/network_manager.cpp` (`scanAvailableNetworks`, called from `esp_wifi_scan_start(nullptr, true)`)

## What is wrong

The scan call uses `block=true`. The caller — typically an HTTP handler rendering the Wi-Fi picker UI — is blocked for up to several seconds while the scan runs.

## Why it matters

- HTTP handler stalls: the portal feels frozen; a second client hitting the same page may time out.
- Blocking inside a handler holds an httpd worker for the duration.
- On concurrent form submits, the blocked scan prevents connectivity-changing actions from proceeding in parallel.

## Consequences on real hardware

- Users double-click the scan button; second click stacks.
- Mobile browsers with short timeouts may reload mid-scan.

## Fix plan

1. **Switch to async scan.** `esp_wifi_scan_start(&cfg, false)`.
2. **Handle the `SYSTEM_EVENT_SCAN_DONE` event** in the existing event handler. Stash results in a member (protected by the manager mutex).
3. **HTTP handler serves cached results** with a header indicating freshness: `X-Scan-Age: 3 seconds`. A "refresh" button triggers a new async scan and returns immediately with 202.
4. **Debounce scans.** If a scan is already in progress, subsequent requests wait for the current one or are rejected with 429 depending on UI expectation.
5. **Document** the new contract in `web-ui.md`.

## Verification

- Web portal stays responsive during a scan.
- Two clients can request the Wi-Fi page simultaneously without stacking.

## Related

- H3 — the Wi-Fi page is in the same monolith; split it out at the same time.
