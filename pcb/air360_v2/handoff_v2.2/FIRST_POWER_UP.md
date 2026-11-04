# First power-up

Use a current-limited bench supply. Wear eye protection the first time power is applied. Numbers below are datasheet values or calculations, not measurements on this board.

## Before power

1. Inspect the board: polarity of C2, C13, D2, D3, D4, D8, LED2, orientation of U1–U7, D1, D5–D7, LED1, Q1–Q5. Look for solder bridges at J1, U1 and U2.
2. With nothing connected, measure resistance to GND at `DC_IN`, `V_BUCK`, `+5V`, `3V3` and `+5V_S`. Stop if any reads near zero.
3. Leave all external modules and sensors disconnected.

| JP1 | Battery | Restart `V_ON` |
|---|---|---|
| Jumper off | 2S LiFePO4 | 6.81 V |
| Jumper on | 2S Li-ion | 7.20 V |

## DC input, LiFePO4 profile

1. Remove the JP1 jumper. Set the supply to 7.5 V, current limit 200 mA, and connect to J3 or J4. Polarity at J4 is marked `+` and `−`.
2. Measure `3V3M` at C18: expect about 3.3 V at once.
3. Wait for the restart delay: 57 s nominal, at least about 34 s. This delay also applies to the first start.
4. Check TP2 (`EN_NODE`) against TP3 (GND): expect it to go high, LED2 to light, then `+5V` near 5 V and `3V3` near 3.3 V. The current limit may need raising to about 500 mA for the ESP32.
5. If current sits at the limit, or a part heats quickly, disconnect and find the fault before going on.

## Threshold check

LED2 (green) is on while the buck runs. It lights after the restart delay once DC is above `V_ON`, stays on as DC falls, and goes out at `V_OFF`. It stays off on USB-only power. Allow up to a minute at each step when looking for `V_ON`.

1. Measure TP1 (`VREF`) against TP3. Datasheet range is 1.208–1.276 V.
2. Lower the supply slowly from 7.5 V. Record the voltage where `+5V` drops: this is `V_OFF`. Expect 6.24 V nominal. The guaranteed range is 5.90–6.59 V.
3. If `V_OFF` is outside 6.1–6.35 V, change R10 using the table below. `V_ON` moves with it.
4. Raise the supply slowly. Record where the board restarts after the delay. Expect 6.81 V with the jumper off, and 7.20 V with it on.

| R10 | `V_OFF` | `V_ON` LiFePO4 | `V_ON` Li-ion |
|---|---|---|---|
| 130 kΩ | 6.01 V | 6.57 V | 6.97 V |
| 124 kΩ, fitted | 6.24 V | 6.81 V | 7.20 V |
| 120 kΩ | 6.41 V | 6.97 V | 7.37 V |
| 115 kΩ | 6.63 V | 7.20 V | 7.59 V |
| 107 kΩ | 7.03 V | 7.60 V | 7.99 V |

Calculated with `Vref` 1.242 V, R9 499 kΩ, R11 787 kΩ, R12 1.13 MΩ, `Vd` 0.35 V.

- `V_OFF = Vref × (1 + R9/R10)`
- `V_ON = V_OFF + R9 × (Vref − Vd) / Rd`, where Rd is R11, or R11 in parallel with R12 for Li-ion
- Delay is about `(C23 in nF + 0.5) / 175` s. C23 is 10 µF.

Do not swap U5 for TLV3011B without rechecking the start-up kick.

## USB

1. With DC disconnected, connect USB-C from a charger or port.
2. Expect `+5V` near VBUS and `3V3` near 3.3 V. LED2 stays off.
3. Flash firmware over native USB. If entering download mode by hand, hold BOOT and press RESET.

## Firmware settings

- Enable INA219 at address `0x41`.
- Keep `power_gate_enabled = 0`. On USB-only power the INA219 reads about 0 V, and an enabled gate would sleep the board.
- Check the RGB LED on GPIO48 with `LED_MODEL_WS2812`.

## Battery

Use a pack with its own BMS.
