# ADR-0009: Nu-Link2-Me PB.12 Is Not Connected

- Status: Accepted / HIL pending
- Date: 2026-07-13
- Supersedes: the PB.12 target-VDD presence-interlock portion of ADR-0008
- Superseded in part by: ADR-0010, which also disables M032 PB.3 sensing; the
  M032-retained statement below is kept as the original decision record

## Context

ADR-0008 assigned PB.12/EADC0_CH12 to a 3.3 V target-rail presence check. The
target board may be independently powered, potentially at 5 V, and a series
resistor alone does not limit the steady-state ADC voltage. Target-VDD sensing
is not required by RL78 Protocol B. The approved immediate change is therefore
to leave PB.12 physically unconnected and remove it from programming entry.

This decision does not approve a 5 V TOOL0 or RESET connection. The existing
Nu-Link2-Me target interface remains limited to the separately approved 3.3 V
electrical configuration until a level-interface design passes HIL.

## Decision

- PB.12/ETMD3_SS is NC in the Nu-Link2-Me RL78 programmer profile. Firmware
  does not select its EADC function, configure its pull control, disable its
  digital path, start EADC conversions, or require a voltage result.
- PB.13/ETMD2_CK remains reserved and unconnected.
- Board I/O declares whether target-VDD sensing is enabled. A board with the
  capability enabled must provide a reader and remains fail-closed on read or
  range failure. A board with it disabled skips only the VDD read/range gate.
- The NuMaker-M032SE profile keeps its existing ADC-based VDD interlock. The
  Nu-Link2-Me profile explicitly disables the capability and provides no fake
  voltage reading.
- Online and KEY1-triggered offline jobs use the same mode-entry function, so
  the disabled Nu-Link2-Me interlock applies identically to both workflows.
- Package integrity, exact target profile, Protocol B handshake, erase
  authorization, bounded recovery and terminal verification remain required.

## Consequences

Nu-Link2-Me no longer has a firmware power-presence interlock. Before attaching
the target, the operator or fixture must verify common ground, the approved
target voltage and the TOOL0/RESET electrical interface independently. An
absent or nonresponsive target is detected by the bounded Protocol B handshake,
not by PB.12. Supporting a 5 V target still requires a separate level-interface
decision and physical verification.

## Evidence

- `firmware/board/m032_board_io.[ch]`
- `firmware/board/m032_numaker_m032se_board.c`
- `firmware/nulink2me/nulink2me_board.[ch]`
- `firmware/nulink2me/nulink2me_board_profile.[ch]`
- Native tests for enabled/disabled sense behavior and PB.12 exclusion.
- Required HIL: PB.12 remains unused, target voltage is measured externally,
  and online/offline Protocol B entry operates without a PB.12 connection.
