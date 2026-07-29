# Architecture Decision Records

| ADR | Status | Decision |
| --- | --- | --- |
| [ADR-0001](ADR-0001-REUSABLE-PLATFORM.md) | Accepted | Product is a reusable programmer platform; device behavior is plug-in/profile isolated |
| [ADR-0002](ADR-0002-HID-AND-SDK-BOUNDARY.md) | Accepted | GUI uses SDK; V1 PLP uses fixed 64-byte HID reports |
| [ADR-0003](ADR-0003-OFFLINE-STORAGE.md) | Accepted / firmware partition superseded | One 64 KiB slot and two metadata pages; ADR-0011 replaces the original monolithic 63 KiB firmware region |
| [ADR-0004](ADR-0004-BOARD-INTERFACE.md) | Accepted / PB.3 portion superseded | Original M032 board-interface profile; ADR-0010 replaces its PB.3 VDD interlock |
| [ADR-0005](ADR-0005-PACKAGE-SECURITY.md) | Proposed / blocked | Production signing, keys and rollback policy require security ownership |
| [ADR-0006](ADR-0006-OFFLINE-ARMED-HID.md) | Accepted / trigger superseded | Offline Armed remains HID-accessible; ADR-0007 replaces power-on trigger |
| [ADR-0007](ADR-0007-RL78-PROTOCOL-B-RESET-TRIGGER.md) | Accepted / HIL pending | M032 external Reset triggers offline Protocol B; G15 4/8 KiB and G16 16/32 KiB profiles |
| [ADR-0008](ADR-0008-NULINK2ME-RL78-PROGRAMMER.md) | Accepted / PB.12 portion superseded | Original M484SIDAE Nu-Link2-Me HIL-candidate profile; ADR-0009 replaces its PB.12 interlock |
| [ADR-0009](ADR-0009-NULINK2ME-PB12-NC.md) | Accepted / M032-retained portion superseded | PB.12 is NC and VDD sense is disabled for Nu-Link2-Me; ADR-0010 extends the same policy to M032 |
| [ADR-0010](ADR-0010-M032-PB3-NC.md) | Accepted / HIL pending | PB.3 is NC and target-VDD sensing is disabled for M032; PA.4 remains Low/physically NC and voltage must be verified externally |
| [ADR-0011](ADR-0011-BRIDGE-BOOTLOADER-APP-UPDATE.md) | Accepted / HIL pending | Both boards use a resident 16 KiB bootloader and fixed CRC-protected application-only USB HID updates |
| [ADR-0012](ADR-0012-M032-TEST-BOARD-UART.md) | Accepted / implemented / HIL pending | M032 TEST BOARD uses PF.2/PF.3 UART transport, PB.9/PB.10 GPIO/TIMER2 software TX/RX TOOL0, PB.8 reset, PC.2 offline/boot button and the existing internal-APROM package partition |

An accepted ADR is changed by a superseding ADR, not by silently rewriting the decision. Proposed ADRs may be refined before acceptance.
