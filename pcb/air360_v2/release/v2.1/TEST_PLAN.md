# Post-assembly test plan

Run `FIRST_POWER_UP.md` first. Record the measured value for each row. Nothing below has been run yet.

## Electrical acceptance

| # | Test | Pass |
|---|---|---|
| 1 | Connect each connector to its real cable, module or sensor | Works; pinout matches `REQUIREMENTS.md` section 7 |
| 2 | 6.2 V at DC, electronic load 3.33 A on +5V | +5V in regulation, no part over rating |
| 3 | Lower DC slowly through `V_OFF` | All +5V loads drop once and stay off until above `V_ON` |
| 4 | After a trip, remove all load | No restart |
| 5 | USB with DC dead, absent and reversed | USB operation continues |
| 6 | USB and DC together, each order of connection | No backfeed into DC or VBUS |
| 7 | Flash, reset and logs over native USB | All work with stock firmware |
| 8 | Compare INA219 voltage and current with a meter | Within INA219 specification at `0x41` |
| 9 | 26 V at DC | No damage; UVLO need not protect that source |
| 10 | 5 V charger at DC, `5V` profile, then a battery profile | Runs on `5V`; off and undamaged on a battery profile |
| 11 | Watch LED2 through tests 3 and 5 | On with the buck, off at `V_OFF`, off on USB only; brightness acceptable with R39 4.7 kΩ |

## Measurements with no acceptance number yet

Set limits from the part ratings before testing.

| Item | Method |
|---|---|
| Buck stability and ripple | Scope at C8–C11, load steps, 6–26 V input |
| SW node ringing | Scope at L1 pad 1 with a short ground spring |
| Thermal rise | Thermocouple or camera on U2, U3, L1, RS1, Q1, Q3 at continuous load |
| Inner2 and via temperature | Thermal camera at peak load near Q1 and U3 |
| 5 V charger dropout | +5V at DC 5 V and 1.4 A |

## Items that only hardware can confirm

- **Wi-Fi:** range with the board in its enclosure. SW1 sits next to the antenna keep-out.
- **LED1:** brightness and colour at 3.3 V, below its 3.7 V rated minimum. ESP32-S3-DevKitC-1 v1.1 feeds this LED from 3.3 V too.
- **Fit:** cables at all headers, shield mounting holes, jack position with the 1 mm shift at J3, and M3 heads at H1–H6.
- **Solar input:** the source voltage at K3 is still unknown. The input is designed to tolerate 26 V.
