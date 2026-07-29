# ADR-0006: Offline Armed Mode Keeps USB HID Available

- Status: Accepted / trigger mechanism superseded by ADR-0007
- Date: 2026-07-11

## Context

An M032 programmer carrying an activated RL78 image must run without the PC GUI
when powered from USB. The same board must remain discoverable when connected to
a PC so the GUI can identify that offline operation is armed and can return the
board to online-only operation.

## Decision

Operating policy and runtime programming state are separate concepts:

- `ONLINE_ONLY` disables autonomous offline triggers but preserves stored images.
- `OFFLINE_ARMED` permits the active package to be started by the configured
  trigger. The first board profile uses power-on autorun after a bounded HID
  session grace period because no external button is assigned.
- USB HID initialization is unconditional in both policies. Opening a PLP
  session suppresses power-on autorun for that boot and exposes the policy,
  active package, autorun flag and policy generation to the GUI.
- A mode change is rejected while a programming operation is active. Arming is
  rejected unless a validated active package, persistent mode store and ready
  target driver are present.
- Returning to `ONLINE_ONLY` does not delete or invalidate the active package.

## Consequences

The GUI can recover a board from offline policy without reflashing M032. A USB
charger does not need to enumerate HID; after the grace period the stored image
provider starts autonomously. A PC has a deterministic interval to open a
session and suppress autorun. Production may later replace power-on autorun with
a fixture input or button without changing the programming engine.

## Evidence

- PLP commands `GET_OPERATING_MODE` and `SET_OPERATING_MODE`.
- Native tests for persistence, HID query/switch, grace period and reboot
  recovery.
- `firmware/m032_bridge/m032_programmer.c` and
  `firmware/storage/m032_package_store.c`.
