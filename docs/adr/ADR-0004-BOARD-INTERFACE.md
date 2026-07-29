# ADR-0004: NuMaker-M032SE RL78 Board Interface

- Status: Accepted for software pin mapping; electrical/HIL verification pending
- Date: 2026-07-11
- Decision owner: User delegated the M032 EVB pin selection to the software architect
- Superseded in part by: ADR-0010 for PB.3 target-VDD sensing and the voltage
  range gate; the PB.3-specific text below is retained as the original record

## Context

The first bridge is the NuMaker-M032SE with M032SE3AE. The board VCOM uses
PB.13/UART0_TXD and PB.12/UART0_RXD. Its only MCU-controlled application LED is
the red PB.14 LED, so that LED must provide all RL78 upgrade indications.

RL78/G15 Protocol B supports TOOL0 one-line UART and dedicated UART at a fixed
115200 bps with 8-bit data and odd parity. The programmer transmit direction
uses two stop bits; the target response direction uses one stop bit. For entry
to flash programming mode, the RL78/G15 manual requires
TOOL0 low before reset release, at least 10 us setup, at least 1 ms hold after
reset release, and completion of initial UART settings within 100 ms.

## Decision

Signals are allocated in the official interface order: TOOL0/UART, RESET, then
VDD.

| Order | Logical signal | M032 function | EVB connector | Arduino | Safe boot state |
| --- | --- | --- | --- | --- | --- |
| 1 | TOOL0 / UART TX | PA.3 / UART1_TXD single-wire | JP5.21/JP5.22, MCU pin 27 | D10 | GPIO input / high impedance |
| 1 | Dedicated UART RX reserve | PA.2 / UART1_RXD | JP5.23/JP5.24, MCU pin 28 | D13 | GPIO input |
| 2 | TARGET_nRESET | PA.5 open-drain GPIO | JP5.17/JP5.18, MCU pin 25 | D8 | Released |
| 3 | TARGET_VDD_SENSE | PB.3 / ADC0_CH3 | JP3.7/JP3.8, MCU pin 4 | D1 | Analog input |
| 3 | TARGET_VDD_ENABLE | PA.4 push-pull GPIO | JP5.19/JP5.20, MCU pin 26 | D9 | Low / disabled |
| 4 | RL78_UPGRADE_LED_n | PB.14 GPIO, active-low | JP6.11/JP6.12, MCU pin 54 | - | High / off |

PB.12 and PB.13 are compile-time forbidden by the board-profile validator.
PB.14 is assigned exclusively to the status LED. Button behavior remains
outside this decision.

The default link is UART1 single-wire on PA.3. PA.2 is reserved so a later
approved board profile can select the RL78 dedicated UART without moving the
connector. RESET, VDD enable, TOOL0 and the reserved RX pin occupy D8, D9, D10
and D13 on NU4; VDD sense occupies D1 on the adjacent NU3 digital header. All
external fixture signals are therefore on the Arduino digital-header side of
the EVB. UART0 remains free for Nu-Link2-Me VCOM diagnostics.

## Electrical contract

- TOOL0 requires a target-VDD-referenced bidirectional buffer/level interface;
  direct 3.3 V push-pull connection is not approved for a 5 V RL78 target.
- PA.5 is a logical active-low open-drain reset output. The fixture must provide
  isolation/pull-up suitable for the selected RL78 VDD.
- PB.3 measures target VDD through a 1:1 divider. The implementation assumes
  3.3 V ADC reference and converts 0..4095 to 0..6600 mV. The fixture baseline
  is 47 kohm over 47 kohm with a local filter capacitor; actual tolerance must
  be calibrated/verified before production.
- PA.4 controls only an external current-limited load switch with a hardware
  default-off pull-down. It must never source target VDD directly from a GPIO.
- The official V1.3 schematic connects VDD through 330 ohm and the red LED to
  PB.14, so PB.14 output low means on and output high means off.
- Programming is rejected outside 2.4 V to 5.5 V or when ADC conversion times
  out.

## Runtime sequence

1. Validate target VDD before driving target communication lines.
2. Assert RESET low.
3. Drive TOOL0 low and wait at least 10 us.
4. Release RESET and keep TOOL0 low for at least 1 ms.
5. Switch PA.3 to UART1 single-wire at 115200 bps.
6. The Protocol B driver must complete initial UART setting within the remaining
   100 ms entry window.

Any failure releases TOOL0 and RESET to their safe states. This ADR does not
authorize unverified Protocol B command frames.

## Implementation

- Pin registry: `firmware/board/m032_board_profile.*`
- Portable sequence and safety checks: `firmware/board/m032_board_io.*`
- NuMicro UART/GPIO/ADC adapter: `firmware/board/m032_numaker_m032se_board.*`
- Single-LED semantic patterns: `firmware/board/m032_status_led.*`
- Native conformance tests validate reserved pins, initialization order, entry
  timing order, out-of-range VDD rejection and LED patterns.

## References

- [NuMaker-M032SE User Manual](https://www.nuvoton.com/export/resource-files/UM_NuMaker-M032SE_EN_Rev1.00.pdf)
- [NuMaker-M032SE V1.3 schematic package](https://www.nuvoton.com/resource-download.jsp?tp_GUID=HL0120190304174045)
- [M031/M032 Series Datasheet](https://www.nuvoton.com/resource-files/DS_M031_Series_EN_Rev1.02.pdf)
- [RL78/G15 User's Manual: Hardware](https://www.renesas.com/en/document/mah/rl78g15-users-manual-hardware)

## Remaining acceptance evidence

Schematic review, buffer direction/turnaround waveform, ADC calibration,
voltage-corner/contention tests and HIL entry/read-signature/program/verify are
still required before `profile_ready` can be enabled.
