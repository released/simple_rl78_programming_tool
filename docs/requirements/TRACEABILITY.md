# Requirements Traceability Matrix

| Field | Value |
| --- | --- |
| Document ID | RTM-RPP-001 |
| Baseline | 1.0-draft |
| Source | `Prompt.txt` and approved architecture decisions |

## 1. Purpose

This matrix proves that the original request is assigned to a normative design and a planned acceptance test. `Designed` means specified, not implemented or hardware-verified.

## 2. System and Architecture Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| SYS-001 | Windows MFC GUI + M032SE3AE + USB HID + Protocol B UART + RL78/G15; PB.3 NC and VDD sense disabled | SAS §§2–5; GSDS §2; FSDS §§1–2; ADR-0010 | Native PB.3-NC/disabled-sense tests, Keil M032 build, INT-001 and HIL-001 | Software path implemented / physical HIL pending |
| SYS-002 | Online programming | SAS §5.2; PPS §§5.4–5.5; GSDS §6.1 | Native composed W25Q16 candidate/target flow; HIL-010 and HIL-NL2-005..006 | Software path implemented / HIL pending |
| SYS-003 | Offline USB-power/button programming, including immediate button-triggered handshake restart before erase | SAS §5.3; FSDS §§8–9; OIS §§11–12; ADR-0006 | Native storage/mode/autorun/immediate-retry tests; HIL-020..024 and HIL-NL2-008 | Core implemented / immediate-retry HIL pending |
| SYS-004 | Factory serial mode, not target bootloader; no target firmware modification | SAS §§1.2, 2.2 | Review gate AR-001; HIL-001 | Designed |
| SYS-005 | M484SIDAE Nu-Link2-Me HS-USB bridge with W25Q16, PB.12 NC/VDD sense disabled, KEY1 offline trigger and four status LEDs | SAS §§2-3; FSDS §§1, 8-10; ADR-0008/0009 | Native board-profile and optional-interlock tests; Keil M484 build; HIL-NL2-001..010 | HIL-candidate path implemented / physical HIL pending |
| SYS-006 | M032/M484 APROM-resident 16 KiB bootloader plus recoverable application-only USB HID update; initial debugger provisioning with required CBS/MBS settings; bootloader excluded from updates | SAS §§5.4, 10.2; PPS §5.7; FSDS §§8.4, 10; ADR-0011 | Native bridge-image/PLP, selected-profile/reported-application-geometry, CONFIG gate, readback and VECMAP tests; APROM flash-algorithm and both boot/application Keil map/image checks; HIL-FWU-001..012 | Software path implemented / physical enumeration and interruption HIL pending |
| SYS-007 | M032 TEST BOARD external 3.3 V UART supports the same online/offline engine, internal 64 KiB package slot, PC.2 trigger, split PB.9 TIMER2 software TX/PB.10 GPIO-edge plus TIMER2 software RX TOOL0, PB.8 reset and recoverable UART ISP application update | ADR-0012; SAS §§2–3; PPS §§2.1, 5.7; FSDS §§1, 7–10 | Native UART framing/profile/board-ID tests, MFC Debug/Release, UART boot/application Keil builds and HIL-MTB-001..012 | G15 Online 20/20 and PC.2 Offline 22/22 passed; G16, LED observation and power-interruption HIL pending |
| ARC-001 | SOLID/layered/interface-based/dependency injection | SAS §§1.4, 3; GSDS §2; FSDS §2 | ARC-STATIC-001 | Designed |
| ARC-002 | GUI must not access HID directly | SAS §3.2; GSDS §1.3; PSDK §1 | Dependency test ARC-STATIC-002 | Designed |
| ARC-003 | Replace HID with CDC/UART/Ethernet/BLE without engine changes | SAS §§3.3, 12.2; PSDK §12 | SDK-TRANSPORT-001 | Designed |
| ARC-004 | Programming Engine communication-independent | SAS §§3.2–3.3; FSDS §6 | FW-UNIT-ENGINE-001 | Designed |
| ARC-005 | Firmware upper layers access hardware through HAL only | SAS §3.2; FSDS §§2–3 | FW-STATIC-HAL-001 | Designed |
| ARC-006 | Future programmer MCU changes HAL/platform only | SAS §12.4; FSDS §18 | Architecture review | Designed |

## 3. Device, Image and Storage Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| DEV-001 | Generic driver operations: connect/signature/info/read/erase/program/verify/reset/disconnect/capability/timeout/retry | SAS §3.3; FSDS §§3, 7 | DRV-CONF-001..012 | Designed |
| DEV-002 | New devices require new driver/profile, zero engine changes | SAS §12.1; FSDS §18 | DRV-PLUGIN-001 | Designed |
| DEV-003 | RL78/G15/G16 Protocol B state/retry/timeout/recovery/error, including bounded Nu-Link2-Me single-wire TX/RX turnaround, TX echo/collision detection, pre-target SRAM image caching, deterministic per-attempt peripheral cleanup, one-second connect recovery, immediate terminal target park, two bounded collision-only full-job retries, 64-entry offline failure history and safe immediate pre-erase button retry | FSDS §§7-8, §13; ADR-0007; ADR-0008 | Native Protocol B/cache/button-retry/recovery/snapshot/history-overflow tests, Keil Nu-Link2-Me build; HIL-RL78B-001..020 and HIL-NL2-008/011 | Implemented in software; history-enabled HIL correlated all 13 program-stage direction collisions in a 25/38 pass run, and the superseded reset-only image executed its reset after all 19 failures in a 9/28 run; bounded full-job retry physical HIL remains pending |
| DEV-004 | Future G16/G23/F24/other Renesas | SAS §12.1 | Profile/driver conformance per device | Designed |
| IMG-001 | Generic Image Builder supports MOT/HEX/BIN; ELF future | SAS §3.3; GSDS §3; OIS §§2, 10 | IMG-PARSE-001..040 | Implemented for MOT/HEX/BIN; ELF reserved |
| IMG-002 | Engine understands only UPI, never MOT/HEX/BIN | SAS §§3.2, 6; OIS §§1–2 | Dependency/unit test IMG-ARCH-001 | Designed |
| IMG-003 | Validate address/CRC/overlap/duplicate/unsupported/option/security/version/device | GSDS §3; OIS §10 | IMG-VAL-001..030 | Designed |
| IMG-004 | Never program after validation failure | SAS §5.4; OIS §10.2 | INT-NEG-001 | Designed |
| PRV-001 | Image providers: online/internal Flash/future SD/USB/network | SAS §3.3; FSDS §§3, 6 | PROVIDER-CONF-001 | Designed |
| STO-001 | Store config/offline/history/statistics/calibration/version/recovery | SAS §6; FSDS §8 | STO-POWER-001..040 | Designed |
| STO-002 | Multiple images/A-B/power-loss/recovery | OIS §§11–12; FSDS §8 | STO-POWER-020..040 | Designed |
| STO-003 | Future encryption | OIS §§4, 9, 19 | Security conformance when enabled | Designed/reserved |

## 4. PC GUI Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| GUI-001 | Main window/device/image/download/offline/monitor/info/project/errors/progress/history/config/language/version/CRC | GSDS §§3–5 | GUI-UNIT/UI-001..030 | Designed |
| GUI-002 | MOT parser/image builder/task queue/logger/dispatcher | GSDS §§3, 7–10 | GUI-UNIT-IMG/TASK/LOG | Designed |
| GUI-003 | Responsive background processing/cancellation | GSDS §§5, 7 | GUI-STRESS-001..010 | Designed |
| GUI-004 | Future plug-ins for device/protocol/parser/report | SAS §§3.3, 12; GSDS §11 | PLUGIN-001..012 | Designed |
| GUI-005 | Multilingual/DPI-stable MFC UI with a maximized initial main-frame state and standard restore/minimize/maximize controls | GSDS §§4.1, 7.2 | UI-DPI-LANG-001 and MFC Debug/Release | Implemented |
| GUI-006 | `FW Upload (USB HID)` tab with board profile, separate app BIN validation, five workflow rows with completed-row gray treatment, Return Boot loader, full HID-path ISP scan/select, explicit next-step guidance when validation is missing, stale-selection invalidation, pre-erase selected-profile/reported-application-geometry binding, byte/percent progress, upload/cancel/run | GSDS §§3, 4.1, 6.3 | GUI-FWU-001..015 and MFC Debug/Release | Implemented / physical transition pending |
| GUI-007 | `History` exposes terminal `Detail`; on-screen Log/History remain active independently of optional disk logging | GSDS §§3, 4.1, 8–10 | GUI-LOG-001..012 and MFC Debug/Release | Implemented |
| GUI-008 | Connected-only `RESET MCU` action ACKs before a normal bridge reset, preserves application CRC, closes stale PC session and requires scan/reconnect; M484 FW `1.0.1.5` uses full-chip reset to clear internal peripheral state while external devices remain powered | GSDS §§4.2, 6.4; PPS §§5.2, 5.10.2; FSDS §8.4 | Native reset command matrix, MFC Debug/Release, HIL reset/re-enumeration and post-reset KEY1 | Software/build path implemented / M484 physical HIL pending |
| GUI-009 | `Offline` directly exposes the shared exact-profile MOT/HEX/BIN input, generates and validates OIP automatically, reports storage-download byte progress, and guides download/activate/arm/physical trigger through five workflow rows with exact package-UUID gating; the validated package is bound to the exact displayed source path, path/profile edits clear it, and a stale asynchronous completion is rejected | GSDS §§3, 4.1, 6.2; OIS §§3–8 | GUI-OFF-001..012, native exact-path binding tests, MFC Debug/Release, Nu-Link2-Me SPI/KEY1 and M032 storage/Reset HIL | Implemented / physical offline HIL pending |
| GUI-010 | Board selector includes exact `M032 TEST BOARD`; HID VID/PID fields are disabled for UART, COM discovery/connection is SDK-owned, FW Upload changes to UART ISP with the same five guided steps, and Scan lists only an `MTB1`-verified bootloader rather than every OS COM port | GSDS §§4.1–4.2, 6.3; ADR-0012 | MFC Debug/Release, serial discovery/identity negative tests and HIL-MTB-001..004 | Implemented / physical VCOM, UART ISP and Application PLP passed |

## 5. PC ↔ M032 Protocol Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| PPS-001 | Header/magic/command/sequence/length/payload/CRC16/max size | PPS §§2–4 | PPS-GOLDEN-001..020 | Designed |
| PPS-002 | ACK/NAK/retry/timeout/version negotiation | PPS §§3, 7, 11 | PPS-RETRY-001..020 | Designed |
| PPS-003 | Discovery/device info/status/log/storage | PPS §§2, 5.2–5.3 | PPS-CMD-0100 suite | Designed |
| PPS-004 | Image download/finalize/activate/offline program | PPS §§5.4–5.5, 8 | PPS-XFER-001..030 | Designed |
| PPS-005 | Program/verify/reset/signature/read flash/config | PPS §§5.5–5.6 | PPS-PROG-001..020 | Designed |
| PPS-006 | Programmer update command family plus non-replay-safe `FW_ENTER_BOOTLOADER` (`0x0506`) | PPS §§5.7, 5.10.7, 7 | Native command state/capability/busy tests; PPS-FWU-001..020 | `0x0506` implemented; staged signed `0x0500`–`0x0505` designed |
| PPS-007 | Error response/reserved commands/binary examples | PPS §§5.1, 9–10; Contracts §3 | PPS-GOLDEN/FUZZ | Designed |
| PPS-008 | State and sequence diagrams/future expansion | PPS §§6, 8, 13–16 | Review + conformance | Designed |
| PPS-009 | Non-replay-safe `RESET_PROGRAMMER` (`0x0007`) with capability/session/busy gates and ACK-before-reset semantics | PPS §§5.2, 5.10.2, 7 | Native command matrix; HIL reset/re-enumeration | Software path implemented / physical HIL pending |
| PPS-010 | Exact 64-byte CRC-valid PLP over M032 TEST BOARD UART at 115200 8-N-1 with bounded magic resynchronization; resident UART ISP reports `MTB1` identity | PPS §§2.1, 5.7; Contracts §§3, 4.3 | Native fragmented/noisy/corrupt stream and board-ID geometry tests; HIL-MTB-001/009 | Implemented / physical Application PLP and `MTB1` UART ISP passed |

## 6. Firmware Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| FW-001 | main/USB/parser/dispatcher/image/storage/CRC/button/LED/device/programmer/RL78/UART/GPIO/timer/log/settings/error/watchdog/scheduler/state | FSDS §§2–14 | FW-UNIT module suites | Designed |
| FW-002 | Fixed APROM boot/application/CRC/storage memory maps, CBS/MBS preflight, config, watchdog and recovery | FSDS §§8.4, 10–12; Contracts §4.3; ADR-0011 | FW-MAP-001, APROM algorithm audit, boot/app Keil map and SRecord image checks, FW-WDG-001, STO-POWER | Maps implemented / physical recovery HIL pending |
| FW-003 | Online and offline share engine | FSDS §§2, 6, 8 | INT-PROVIDER-001 | Designed |
| FW-004 | App invalidates CRC page before bootloader-handoff ACK, resets after response completion/fallback; bootloader verifies each write after `ReadData`; both handoffs mask IRQ before VECMAP; invalid app remains at `0416:3F00` | FSDS §8.4; PPS §5.7; ADR-0011 | Native callback/dispatch/readback/VECMAP tests; HIL-FWU-002..008 | Software path implemented / HIL pending |
| FW-005 | Normal programmer reset preserves application CRC, ACKs before bounded board-defined reset through vector page zero, and rejects active transfer/programming work; M484 FW `1.0.1.5` uses full-chip reset and M032 retains CPU reset | FSDS §8.4; PPS §§5.2, 5.10.2 | Native callback/dispatch matrix; M032/M484 Keil builds; HIL reset/re-enumeration and post-reset KEY1 | Software/build path implemented / physical HIL pending |
| FW-006 | M032 TEST BOARD app uses UART0 PLP, independent TIMER1 entry delays and Nu-Link2-Me-style ownership for PB.9 8-O-2 software TX plus PB.10 GPIO-edge/TIMER2 8-O-1 software RX; start/data/parity TX polls the 417-tick TIMER2 flag inside a bounded approximately 87 us interval and restores interrupts/arms RX at the first stop bit, while the M032 falling edge directly starts one periodic 417-tick data-sampling schedule; RX remains disabled during entry/TX, with first-stop-bit turnaround and bit-level collision detection; UART0 buffer critical sections mask only `UART02_IRQn`; internal Package-slot reads are memory-mapped after SYS lock, while Protocol B compares pre-entry dense-image CRC with the post-prepare programming-unit CRC before PASS; PB.8 reset and PC.2 trigger remain unchanged; Red blinks during Online/Offline programming, terminal failure is solid Red and PASS is solid Green; UART boot is 16 KiB and app image is fixed `0xBC00` | FSDS §§1.3, 7–10; ADR-0012 | Build-time timing/pin-mux/GPIO-RX/polled-TX/single-schedule/no-UART4/UART02-only-mask/memory-mapped-read/CRC/LED guards, native changed-image rejection/profile/storage tests, clean UART boot/app Keil maps/images and HIL-MTB-001..012 | `1.0.0.14` deterministic TX passed 20/20 G15 8 KiB Online and 22/22 PC.2 Offline captures with exact payloads/final verify ACKs; `1.0.0.15` LED policy built, physical LED observation pending |

## 7. Offline Package Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| OIS-001 | Header/version/target/project/FW/build/size/CRC/metadata | OIS §§4–6 | OIS-GOLDEN-001..020 | Designed |
| OIS-002 | Code/data/option/security/image signature | OIS §§7–9 | OIS-SEC-001..020 | Designed |
| OIS-003 | Multiple images/A-B/recovery/power loss | OIS §§11–12 | STO-POWER suite | Designed; multi-image reserved |
| OIS-004 | Generic UPI and provider stream | OIS §§2, 6–7, 13 | OIS-PROVIDER-001 | Designed |
| OIS-005 | Bridge application BIN is not OIP and does not consume/replace M032 Package slot A | OIS §§1.3, 12.1; ADR-0011 | Negative artifact-routing tests; M032 storage-preservation HIL | Designed / software boundary implemented |

## 8. SDK Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| SDK-001 | GUI→SDK→HID boundary | PSDK §§1–2 | Dependency/SDK integration | Designed |
| SDK-002 | API/error/version/callback/progress/cancel/log/config | PSDK §§3–10 | SDK-ABI/UNIT-001..050 | Designed |
| SDK-003 | C/C++/Python/C#/LabVIEW | PSDK §11 | Binding conformance suites | Designed |
| SDK-004 | Reconnect/reconciliation/idempotency | PSDK §§6–7 | SDK-FAULT-001..020 | Designed |
| SDK-005 | SDK-only PLP-to-ISP bridge update path, exact M032/M484 app profiles, full-path bootloader discovery, authoritative geometry and CBS/MBS probe/bind before erase, program/readback/run and progress/cancel | PSDK §§2, 4.5–4.6, 7 | Native image/protocol/geometry/CONFIG/readback tests; GUI mock; HIL-FWU-001..012 | Implemented / physical HIL pending |
| SDK-006 | Win32 COM transport implements partial-read-safe application PLP and raw Nuvoton ISP packets, drains complete requests before closing the external-VCOM handle, releases the Application handle after boot handoff, and requires `MTB1` plus exact geometry before M032 TEST BOARD erase | PSDK §§2–4, 12; ADR-0012 | Native UART framer/board-ID tests, MFC Debug/Release and HIL-MTB-001..004/009 | Implemented / physical Application PLP and UART ISP update passed; interruption recovery pending |

## 9. Cross-cutting and Production Requirements

| ID | Requirement | Normative design | Planned acceptance | Status |
| --- | --- | --- | --- | --- |
| CFG-001 | Config separate from source: timeout/retry/speed/LED/language/auto-run/factory | SAS §6.2; FSDS §11; GSDS §9; Contracts §7 | CFG-001..020 | Designed |
| LOG-001 | Common TRACE/DEBUG/INFO/WARNING/ERROR and categories | Contracts §6; SAS §6.3 | LOG-001..015 | Designed |
| LOG-002 | GUI `Save log to file` defaults disabled on first use or when its key is missing; the user choice persists in `rl78_programmer.ini` as `SaveLogToFile=0/1`; every restored enabled launch creates a new unique UTF-8 session log beside the EXE without appending to an earlier file; disabling stops disk writes immediately, `Clear Log` is screen-only, and create/write errors are visible, clear the checkbox for the current run and never fall back to another path; terminal records include mode/job/result/stage name+ID/error name+hex/detail/attempt/elapsed/progress | GSDS §§4.1, 8–10 | GUI-LOG-001..012, missing-key/persistence/relaunch/no-append/read-only/write-failure tests and MFC Debug/Release | Implemented |
| ERR-001 | Unified USB/image/CRC/Flash/program/timeout/device/driver/storage/offline/unknown errors | Contracts §3 | ERR-CATALOG-001 | Designed |
| PROD-001 | Batch/auto/stats/count/time/yield/pass/fail/operator/barcode/CSV | GSDS §§3, 5.4, 14; Production Spec | PROD-001..030 | Designed |
| PROD-002 | Password/project lock/factory mode/serial/QR/history | GSDS §§3, 10, 14 | PROD-SEC/UI suites | Designed |
| PROD-003 | Future MES | GSDS §3; Production Spec | MES-MOCK-001..012 | Designed |
| SEC-001 | Signature, authorization, anti-rollback and secret handling | SAS §8; OIS §9; Contracts §§3, 6 | SEC-001..030 | Designed; algorithm/key policy TBD |

## 10. Documentation and Directory Requirements

| ID | Requirement | Evidence | Status |
| --- | --- | --- | --- |
| DOC-001 | SAS | `docs/architecture/SAS.md` | Present |
| DOC-002 | PPS | `docs/protocol/PPS.md` | Present |
| DOC-003 | FSDS | `docs/firmware/FSDS.md` | Present |
| DOC-004 | GSDS | `docs/gui/GSDS.md` | Present |
| DOC-005 | OIS | `docs/offline/OIS.md` | Present |
| DOC-006 | PSDK | `docs/sdk/PSDK.md` | Present |
| DIR-001 | PC/Firmware/Shared/Protocol/Document/Test/Tools/Build | Root structure and READMEs | Present |
| DOC-007 | Mermaid/sequence/state/class/flow/risk/test/future | Six normative specifications | Present |

## 11. Open Acceptance Blockers

The NuMaker-M032SE software pin profile is accepted by ADR-0004; ADR-0010 makes
PB.3 NC and disables its VDD preflight. The
M484SIDAE Nu-Link2-Me pin/storage/indicator profile and fixed 3.3 V target
policy are accepted by ADR-0008; ADR-0009 makes PB.12 NC and disables VDD
sensing. The formal build exposes that bounded path as a HIL candidate, but
physical HSUSB, SPIM, both NC sense pins, externally verified target voltage,
TOOL0, reset and RL78
erase/program/verify evidence remain open. Exact RL78 parts,
USB production identity, security/key policy and MES/operator policy also
remain open as listed in `HANDOFF.md` and the ADR index. Requirements that
depend on them cannot be declared hardware-verified yet.

Bridge updater hardware evidence is also open: application and bootloader USB
enumeration/re-enumeration, invalid-vector/CRC stay-in-boot behavior, update
interruption recovery and successful application launch have not been run on
either physical board. The current application-only development path does not
satisfy the future staged/signed production-update requirement.
