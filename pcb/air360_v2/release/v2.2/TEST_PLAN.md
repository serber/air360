# Post-assembly test plan

Run `FIRST_POWER_UP.md` first. Record the measured value for each row. Nothing below has been run yet.

## Electrical acceptance

| # | Test | Pass |
|---|---|---|
| 1 | Connect each connector to its real cable, module or sensor | Works; pinout matches `REQUIREMENTS.md` section 7 |
| 2 | 6.2 V at DC, electronic load 1.5 A on +5V | +5V in regulation, no part over rating |
| 3 | Lower DC slowly through `V_OFF` | All +5V loads drop once and stay off until above `V_ON` |
| 4 | After a trip, remove all load | No restart |
| 5 | USB with DC dead, absent and reversed | USB operation continues |
| 6 | USB and DC together, each order of connection | No backfeed into DC or VBUS |
| 7 | Flash, reset and logs over native USB | All work with stock firmware |
| 8 | Compare INA219 voltage and current with a meter | Within INA219 specification at `0x41` |
| 9 | 5 V at DC | Board stays off, no damage |
| 10 | 12 V at DC, short test | No damage; not an operating mode |
| 11 | Watch LED2 through tests 3 and 5 | On with the buck, off at `V_OFF`, off on USB only; brightness acceptable |

## Measurements with no acceptance number yet

Set limits from the part ratings before testing.

| Item | Method |
|---|---|
| Buck stability and ripple | Scope at C8, load steps, 6–8.4 V input |
| 3V3 stability | Scope at C16: AMS1117 output with ceramic capacitors, load steps |
| UVLO first start | Connect a charged pack several times: board must start after the delay each time |
| SW node ringing | Scope at L1 pad 1 with a short ground spring |
| Thermal rise | Thermocouple or camera on U2, U3, L1, RS1, Q1, Q3 at continuous load |
| 3V3 on USB | Minimum USB voltage with AMS1117 at full ESP32 load |
| U3 pin 2 solder joint | Inspect, because the pad has a via |

## Items that only hardware can confirm

- **Modem:** whether your modem runs from +5V at 1.5 A through its transmit bursts. Watch +5V with a scope.
- **Wi-Fi:** range with the board in its enclosure. SW1, marked `RESET`, sits next to the antenna keep-out.
- **LED1:** brightness and colour at 3.3 V, below its 3.7 V rated minimum. ESP32-S3-DevKitC-1 v1.1 feeds this LED from 3.3 V too.
- **Fit:** cables at all headers, shield mounting holes, jack position at J3, and M3 heads at H1–H6.
