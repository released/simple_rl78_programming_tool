# AGENTS.md

## Project Purpose

This workspace specifies a reusable production programmer platform. RL78/G15 over RL78 Protocol B and an M032SE3AE USB HID bridge are the first product configuration, not permanent core dependencies.

## Current Phase

The project entered implementation phase on 2026-07-11 by explicit user request.

- Implement PC GUI, Programmer SDK, shared contracts/tests, M032 firmware core and board adapters against the approved specifications.
- Keep the six normative specifications, shared contracts, requirement traceability, and decisions synchronized with implementation changes.
- Hardware-destructive behavior remains fail-closed until the board interface, storage map and exact target profile are approved and verified.
- Firmware C remains strict C90-friendly: declarations at block start and English comments only.

## Normative Document Order

1. `docs/contracts/` for shared identifiers, errors, data definitions, and compatibility rules.
2. `docs/protocol/PPS.md` for the PC-to-programmer wire contract.
3. `docs/offline/OIS.md` for the offline package contract.
4. `docs/sdk/PSDK.md` for the reusable host SDK boundary.
5. `docs/architecture/SAS.md`, `docs/firmware/FSDS.md`, and `docs/gui/GSDS.md` for component behavior.
6. `docs/requirements/TRACEABILITY.md` for acceptance ownership.

If two documents conflict, resolve the conflict at the highest applicable authority and record the decision in `docs/adr/`.

## Architecture Rules

- Keep UI, application, domain, SDK, transport, device driver, image provider, and HAL layers interface-based.
- The GUI must not access USB HID directly.
- The Programming Engine must not parse MOT/HEX/BIN and must not know the transport or target MCU type.
- Target-specific rules belong in a device driver/profile plug-in.
- Firmware upper layers must not access USB, UART, GPIO, timer, flash, or watchdog registers directly.
- Shared numeric contracts must have one truth source and explicit reserved ranges.
- Destructive operations require validation, an explicit policy, bounded recovery, and auditable results.

## Documentation Verification

Before delivery:

1. Check Markdown links and Mermaid fence pairing.
2. Check that all six required documents exist.
3. Check contract IDs and version statements for conflicts.
4. Check that every mandatory prompt topic is traced.
5. Check that no unintended binaries, generated artifacts, vendor sources, or unrelated files were added.
6. Build native tests and PC Debug/Release after relevant changes; build Keil firmware after board integration changes.
7. Update `README.md`, `HANDOFF.md`, and `SELF_CHECK.md` with actual results only.
