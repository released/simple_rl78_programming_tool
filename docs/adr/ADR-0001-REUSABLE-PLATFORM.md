# ADR-0001: Reusable Programmer Platform

- Status: Accepted
- Date: 2026-07-11

## Context

The first product programs RL78/G15 through Protocol B, but future G16/G23/F24 and other Renesas devices are required without rewriting GUI/engine/storage.

## Decision

Programming Engine depends on `IDeviceDriver` and `IImageProvider`. Device profiles/drivers own memory, target protocol, capabilities, timeouts, retries and status mapping. GUI renders generic capabilities and project schemas. UPI separates source parsing from programming.

## Consequences

Initial implementation has more contracts and conformance work, but target additions do not add target-name branches to GUI or Engine. A device release must ship profile/driver/conformance evidence together.

## Risks and Tests

Risk is an abstraction that omits a target-specific need. Mitigate with capability/TLV extension points and prove by adding a simulated second driver without modifying Engine/GUI core.

