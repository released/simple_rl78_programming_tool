# NuMaker-M032SE board profile

The accepted software mapping is:

| Signal | Pin | Header | Arduino |
| --- | --- | --- | --- |
| TOOL0 / UART1 single-wire TX | PA.3 | JP5.21/JP5.22 | D10 |
| Dedicated UART RX reserve | PA.2 | JP5.23/JP5.24 | D13 |
| RL78 RESET, active-low open-drain | PA.5 | JP5.17/JP5.18 | D8 |
| Unused target-VDD sense | PB.3 | JP3.7/JP3.8 | D1 (NC) |
| External target-power enable | PA.4 | JP5.19/JP5.20 | D9 |
| RL78 upgrade status LED, active-low | PB.14 | JP6.11/JP6.12 | - |

PB.12/PB.13 remain dedicated to Nu-Link2-Me VCOM. PB.14 is the only
MCU-controlled EVB application LED and is assigned exclusively to RL78 upgrade
status. Button behavior is deferred.

All active external fixture signals use D8/D9/D10/D13 on the NU4 Arduino
digital header. D1/PB.3 is NC, and PB.14 is already connected to the onboard
LED.

`m032_board_profile.*` is the auditable registry. `m032_board_io.*` implements
safe state and the official RL78/G15 entry timing. The NuMicro register adapter
in `m032_numaker_m032se_board.*` configures UART1 single-wire at 115200 bps,
active-low open-drain reset and an active-high external power-enable output
that starts Low. It leaves PB.3 unused and performs no target-VDD measurement.
The V1.3 board schematic makes PB.14 active-low; initialization drives it high
so the LED remains off.

`m032_status_led.*` implements the single-LED contract:

| State | PB.14 LED pattern |
| --- | --- |
| READY | 100 ms heartbeat every 2 s |
| IMAGE TRANSFER | 250 ms on / 250 ms off |
| PROGRAMMING / RECOVERY | 125 ms on / 125 ms off |
| PASS | Solid on |
| FAIL / CANCELLED | Double flash every 1 s |
| OFF / boot default | Off |

Do not connect TOOL0 or RESET directly to a higher-voltage target without the
target-referenced buffer/isolation described by ADR-0004/ADR-0010. Measure
target VDD externally before connection. PA.4 may drive only an external
current-limited load-switch enable and remains physically NC in this profile.
