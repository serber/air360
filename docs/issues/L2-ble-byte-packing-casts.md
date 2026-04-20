# L2 — Unreadable byte-packing casts in `ble_advertiser`

- **Severity:** Low
- **Area:** Code readability
- **Files:**
  - `firmware/main/src/ble_advertiser.cpp` (`buildPayload`)

## What is wrong

The little-endian byte packing code is dense with casts:

```cpp
buf[offset]      = static_cast<std::uint8_t>(static_cast<std::uint16_t>(ival) & 0xFFU);
buf[offset + 1U] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(ival) >> 8U);
```

Repeated for 16-bit signed, 16-bit unsigned, and 24-bit unsigned. Hard to read, easy to introduce an endian or shift bug when adding a new BTHome object ID.

## Why it matters

- Readability of hot-path serialization code matters.
- A byte-order bug here would produce nonsense BTHome values silently (the consumer would interpret whatever it gets).

## Consequences on real hardware

- Correct today; fragile under edit.

## Fix plan

1. **Extract helpers:**
   ```cpp
   inline void writeLe16(std::uint8_t* buf, std::uint16_t v) {
       buf[0] = static_cast<std::uint8_t>(v & 0xFFU);
       buf[1] = static_cast<std::uint8_t>(v >> 8U);
   }

   inline void writeLe24(std::uint8_t* buf, std::uint32_t v) {
       buf[0] = static_cast<std::uint8_t>(v & 0xFFU);
       buf[1] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
       buf[2] = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
   }
   ```
2. **Rewrite `buildPayload` using them:**
   ```cpp
   if (bte.value_bytes == 2U) {
       if (bte.is_signed) {
           const auto ival = static_cast<std::int16_t>(std::clamp(raw, -32768.f, 32767.f));
           writeLe16(&buf[offset], static_cast<std::uint16_t>(ival));
       } else {
           writeLe16(&buf[offset], static_cast<std::uint16_t>(std::clamp(raw, 0.f, 65535.f)));
       }
   } else {
       writeLe24(&buf[offset], static_cast<std::uint32_t>(std::clamp(raw, 0.f, 16777215.f)));
   }
   ```
3. **Unit test the helpers** — trivial but valuable.

## Verification

- Output of `buildPayload` is byte-identical before and after.
- Code line count decreases.

## Related

- M8 — natural companion refactor.
