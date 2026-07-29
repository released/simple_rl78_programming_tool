# ADR-0007: Protocol B Profiles and Reset-Triggered Offline Programming

- Status: Accepted / HIL pending
- Date: 2026-07-11
- Supersedes: the power-on-trigger portion of ADR-0006

## Context

The M032 EVB has no spare application button. The required offline workflow is
to download and activate an OIP with the GUI, arm Offline mode, connect the
RL78 target, and press the existing M032 reset button. G15 4/8 KiB and G16
16/32 KiB are the first target set.

## Decision

- Offline programming starts only when persistent `OFFLINE_ARMED` policy,
  autorun permission, a valid active package, and an M032 external pin-reset
  cause are all present. Power-on reset alone does not start the target job.
- HID remains initialized. A 2-second session grace interval lets the GUI
  inspect the offline state and suppress a not-yet-destructive offline job.
- If Protocol B mode handshaking fails before erase, the driver releases the
  target pins, waits 500 ms, and retries indefinitely. It never retries an
  unknown destructive outcome automatically.
- Online and offline jobs use the same image-provider, Protocol B driver,
  stages, status mapping, cancellation rules, and PB.14 LED manager.
- Four exact profiles are supported: G15 4 KiB, G15 8 KiB, G16 16 KiB, and
  G16 32 KiB. The target-reported size code must match before erase permission.
- The driver implements the official serial-programming flow independently.
  The sample `protocol_b.c` is retained only as an external behavioral
  reference and is not copied into the firmware.
- Protocol B command `0x63` is not supported for G15/G16. OIP packages carrying
  Data Flash content are rejected before mode entry; Code Flash, option bytes
  (`0xC0-0xC3`) and security ID (`0xC4-0xCD`) are handled through the full code
  image stream.

## Consequences

Reset is both the M032 reboot and the offline trigger, so no extra button pin is
required. The active package remains reusable after target connection failure
or M032 power interruption. Physical release still requires electrical and
logic-analyzer/HIL evidence for TOOL0 direction, UART framing, reset, VDD and
all four target-size profiles.

## Evidence

- `firmware/target/rl78_protocol_b.c`
- `firmware/target/m032_rl78_target_binding.c`
- `firmware/m032_app/m032_numicro_main.c`
- Native protocol simulator tests for success, 500 ms retry, size mismatch,
  unsupported Data Flash and reset-trigger gating.
- Controlled documents in `docs/reference/renesas_rl78_protocol_b/`.
