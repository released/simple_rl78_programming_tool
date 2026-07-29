# Verification

`native/test_main.cpp` verifies integrity golden answers, PLP frames, S-record
parsing, OIP validation, firmware negotiation/session behavior, target
fail-closed behavior, offline job terminal state and the M032 HID adapter.
It also validates the NuMaker pin registry, safe initialization order,
RL78/G15 TOOL0/reset entry timing, PB.3-NC/disabled-sense policy, the reusable
optional-sense capability and PB.14 single-LED upgrade patterns.

Hardware-in-loop validation remains pending until the ADR-0004 electrical
interface, ADR-0003 storage profile and exact RL78/G15 target are approved.
