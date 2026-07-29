# M032 TEST BOARD Wiring and Firmware

This note is for the generic `M032 TEST BOARD` M032SE3AE fixture profile.
Customer names and private source-document filenames are intentionally omitted.

The schematic net names, not the earlier proposed GPIO names, are
authoritative:

| Fixture net | Actual M032 pin | Function |
| --- | --- | --- |
| A11 | PB.10, package pin 61 | RL78 TOOL0 GPIO/TIMER2 software receive |
| A10 | PB.9, package pin 62 | RL78 TOOL0 TIMER2/GPIO transmit drive |
| A9 | PB.8, package pin 63 | RL78 RESET# |
| TH | PB.11, package pin 60 | Not used by the programmer |
| SW | PC.2, package pin 38 | Offline trigger / forced UART ISP |
| LED-R | PA.11, package pin 8 | Active-low programming/fail/ISP LED |
| LED-G | PA.10, package pin 9 | Active-low ready/PASS LED |

Do not wire PB.11 as A11.

## RL78 Connection

```text
PB.10 (GPIO RX input)  -- 47 ohm --+
                                  +---- RL78 TOOL0
PB.9  (TX open-drain)  -- 47 ohm --+

PB.8  (RESET open-drain) -- optional 100 ohm -- RL78 RESET#
M032 GND -------------------------------------- RL78 GND
M032 3.3 V ------------------------------------ RL78 VDD (3.3 V target only)
```

PB.10 and PB.9 are not shorted together on the MCU side of the two 47-ohm
resistors. Keep the fitted 30-kohm pull-ups on A11, A10 and A9. A 1-kohm
TOOL0 pull-up to the actual 3.3 V RL78 target rail is recommended.

The firmware drives PB.9 only Low or high impedance with TIMER2 timing. PB.10
uses a GPIO falling-edge interrupt and TIMER2 sampling. Its receive
interrupt is disabled during entry Low and transmit data, then enabled when
PB.9 enters the first stop bit. PB.8 is also Low or high impedance.
The RL78 programming link uses 115200 baud, 8 data bits, odd parity; bridge
transmit uses two stop bits and software receive validates one stop bit.

## PC/VCOM Connection

| JP22 | Net / M032 pin | Connect to |
| ---: | --- | --- |
| 3 | GND | VCOM GND |
| 5 | VDD3V3 | VCOM 3.3 V reference, if required |
| 6 | MCU_RX / PF.2 / UART0_RXD | VCOM TX |
| 7 | MCU_TX / PF.3 / UART0_TXD | VCOM RX |

Use a 3.3 V TTL VCOM adapter at 115200, 8-N-1. Do not connect RS-232 voltage
levels or a 5 V UART adapter.

## APROM Images

Initial debugger provisioning uses:

- UART resident bootloader:
  `SampleCode\ISP_UART\KEIL\obj\ISP_UART.hex`
- Combined bootloader and application:
  `SampleCode\RL78_Programmer\KEIL\obj\M032TestBoardBridge_factory.hex`
- GUI UART-ISP application update:
  `SampleCode\RL78_Programmer\KEIL\obj\M032TestBoardBridge.bin`

Projects:

- `SampleCode\ISP_UART\KEIL\ISP_UART.uvprojx`
- `SampleCode\RL78_Programmer\KEIL\M032_TEST_BOARD_APP.uvprojx`

Build both projects and verify their partitions with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_m032_test_board_keil.ps1
```

The bootloader occupies `0x00000000-0x00003FFF`. The application BIN is
exactly `0xBC00` bytes and maps to `0x00004000-0x0000FBFF`. Metadata occupies
`0x0000FC00-0x0000FFFF`; the 64 KiB RL78 offline package slot occupies
`0x00010000-0x0001FFFF`.

## Operation

1. Select `M032 TEST BOARD` in the GUI.
2. Select the correct COM port and connect. Application communication uses
   PLP over UART at 115200 8-N-1.
3. `Program` performs online RL78 programming.
4. `Offline` loads an RL78 MOT/HEX/BIN, generates OIP, downloads it to
   internal APROM, activates it and arms Offline mode.
5. Press PC.2 once to start one offline programming job.
6. To update the bridge application, load and validate the fixed
   `M032TestBoardBridge.bin`, click `Return Boot loader`, and then click
   `Scan Bootloader`.
7. A Windows COM-port listing alone is not a bootloader result. Scan accepts
   the port only after a NuvoISP response with board ID `MTB1`.
8. Click `Upload Firmware`, then `Run Application`.
   The corrected resident bootloader verifies the application CRC, performs a
   full-chip reset, verifies the CRC again and maps vectors to application
   address `0x4000`; a board power cycle is not part of the normal flow.
9. If the application cannot be connected, hold PC.2 while resetting or
   powering the M032 to force UART ISP, then scan again.

The UART bootloader reports board ID `0x3142544D` (`MTB1`). The GUI verifies
that ID and the complete application geometry before erase.
Application firmware `1.0.0.13` gives the PB.9/PB.10 TOOL0 GPIO/TIMER2 state
machine priority over PC UART0. PB.10 falling-edge detection is disabled during
entry and transmit, then enabled when PB.9 releases for the first stop bit;
the validated falling edge starts one periodic 417-tick schedule whose first
event samples data bit 0, followed by parity and stop checks. Only
PB.9-Low/PB.10-High during an actively transmitted Low bit reports
`TARGET_LINK_DIRECTION_COLLISION`. Receive-frame faults report
`TARGET_LINK_UART_FRAMING`, and Online performs at most three complete
handshakes before erase. Entry Low cannot enter an RX FIFO because software RX
is not armed until after entry release. TIMER1 supplies the reset/TOOL0
microsecond entry delays without stopping the SysTick millisecond deadline
clock, matching the independent-clock behavior of Nu-Link2-Me. The GUI now
drains the VCOM transmit
queue before closing it after `Run Application`. The application handoff is
also resident-bootloader code; a board containing an older resident
bootloader must be provisioned once with the newly built
`M032TestBoardBridge_factory.hex`. Updating only `M032TestBoardBridge.bin`
cannot replace addresses `0x0000-0x3FFF`.
Package-slot reads use memory-mapped APROM access. Do not replace them with
`FMC_Read()`: TOOL0 attempt preparation restores SYS write protection, and the
protected FMC trigger could otherwise return stale `0xFFFFFFFF`. Protocol B
also compares the pre-entry dense-image CRC with the bytes loaded into all
programming units before accepting target verify/PASS.
PC UART0 report-buffer protection masks only `UART02_IRQn`; it never disables
all interrupts. This keeps priority-zero TIMER2 running while the GUI polls
status or the bridge copies a 64-byte PLP request/response. Do not reintroduce
PRIMASK or `__disable_irq()` in `m032_test_uart_port.c`: losing one TIMER2
overflow lengthens one TOOL0 symbol by one 8.7 us bit interval.
The Application shows a slow Green ready blink, flashes Red at 125 ms on /
125 ms off during both Online and Offline programming, latches solid Green on
PASS and latches solid Red on FAIL/CANCELLED/interrupted/outcome unknown.
UART ISP is a separate execution context: it waits with Red on and pulses
Green briefly for each accepted ISP packet.

## Verification Status

The PC Debug/Release builds, 243/243 native conformance tests, UART bootloader
Keil Release and M032 TEST BOARD application Keil Release pass. Physical VCOM
enumeration and UART ISP upload have passed. Firmware `1.0.0.7` transmitted
three valid `0x3A` mode bytes and G15 returned `0x36` each time, but UART4
returned the stale entry-frame `0x00` first. Firmware `1.0.0.8` still failed
the same way because its pre-TX purge occurred only about 53 us after TOOL0
release, shorter than the 95.5 us worst-case 8-O-1 frame. Firmware `1.0.0.10`
removed UART4 and used the Nu-Link2-Me software-RX ownership model, but physical
testing decoded valid `0x3A -> 0x36` on all three attempts while the bridge
reported framing. Firmware `1.0.0.11` removes the half-bit interrupt and second
timer restart that accumulated excessive M032 Cortex-M0 latency. Its physical
rerun completed all 2,048 G15 program units and the final verify ACK, but the
decoder proved that all 8,192 payload bytes were `0xFF`; the GUI PASS was
therefore not a valid image result. Firmware `1.0.0.12` replaces protected
FMC-command reads with memory-mapped APROM reads and adds the Protocol B
pre-entry/transmitted-image CRC comparison. Physical `1.0.0.12` testing then
confirmed exact P20/P21 payload and complete 8 KiB transactions, but a nine-run
warm test passed only five runs. Failed captures contained one PA.9/TOOL0
symbol extended by exactly one 115200 bit interval while a PC status poll was
being serviced. Firmware `1.0.0.13` replaces global PC-UART critical sections
with UART02-only IRQ masking. Repeat Online programming, repeated PC.2 Offline
programming, LED behavior and interrupted UART-ISP recovery. Firmware
`1.0.0.14` has passed 20/20 captured G15 8 KiB Online jobs and 22/22 captured
PC.2 Offline jobs with exact payloads and final verify ACKs. Firmware
`1.0.0.15` changes only the Application LED mapping; physical LED acceptance
and interrupted UART-ISP recovery remain HIL gates.
