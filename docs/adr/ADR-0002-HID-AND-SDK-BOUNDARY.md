# ADR-0002: HID and SDK Boundary

- Status: Accepted
- Date: 2026-07-11

## Context

M032 USB HID enables zero-install Windows operation and aligns with reference projects. Direct HID in MFC would couple UI to transport and duplicate protocol/retry logic in future clients.

## Decision

The GUI uses only Programmer SDK. SDK owns PLP and `ITransport`; Win32 HID is the V1 adapter. PLP uses fixed 64-byte reports with 52-byte payload, report CRC16, sequence/replay and asynchronous event/query reconciliation.

## Consequences

All clients share behavior and future transports replace one adapter. HID chunk throughput is lower than bulk transfer but target Protocol B at 115200 bps and production reliability favor the simple class-driver path for V1.

## Risks and Tests

Surprise removal and Win32 report-ID sentinel handling are isolated in adapter tests. Golden 64-byte vectors and virtual-programmer faults are required.

