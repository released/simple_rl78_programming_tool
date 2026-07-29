# Self check

## Implemented boundaries

- [x] The GUI main window opens maximized initially without disabling the
      standard restore, minimize or maximize controls.
- [x] GUI has no direct USB HID or Win32 COM access.
- [x] Image parser is outside the firmware Programming Engine.
- [x] Online and offline flows converge on one firmware job core.
- [x] Target and storage behavior are callback interfaces.
- [x] Default hardware-destructive behavior fails closed.
- [x] Firmware implementation is allocation-free and C90-compatible.
- [x] Configuration uses a portable INI file rather than Registry storage.
- [x] `Save log to file` defaults disabled on first use or when its key is
      missing, and the user choice persists in portable `rl78_programmer.ini`
      as `[Programmer]` `SaveLogToFile=0/1`; it controls only diagnostic file
      logging, not the always-on on-screen Log/History views.
- [x] A persisted enabled state creates a new uniquely named UTF-8 session file
      beside the EXE on every launch and never appends to an earlier log file.
- [x] Disabling file logging stops disk writes immediately, `Clear Log` affects
      only the screen, and file create/write failures remain visible, clear the
      checkbox for the current run and do not silently fall back to another
      path.
- [x] Terminal diagnostics include mode/job/result, symbolic and numeric stage,
      named and hexadecimal error, detail, attempt, elapsed time and final
      progress; History exposes the terminal `Detail`.
- [x] ADR-0004 software pin mapping is accepted and implemented.
- [x] Target image classification is profile-driven rather than fixed in the builder.
- [x] Non-BIN target images are content-detected as Motorola S-record (`S`) or
      Intel HEX (`:`), then validated strictly without cross-format fallback;
      `.bin` remains explicit raw input.
- [x] Online Only/Offline Armed policy is queryable and switchable over the
      selected HID or UART PLP transport.
- [x] Offline package and operating mode use a single-slot/two-page journaled store.
- [x] NuMicro HID endpoint/descriptor/main and guarded FMC adapters compile.
- [x] GUI `FW Upload (USB HID/UART ISP)` remains SDK-only and separates bridge
      APP BINs from RL78/OIP images.
- [x] Program and FW Upload expose percentage and loaded/programmed/remaining
      KiB indicators; FW Upload scan reports `Load + Validate` as the next step
      whenever `Upload Firmware` is blocked by a missing validated image.
- [x] Program and Offline share one exact-profile MOT/HEX/BIN input and one
      automatically generated OIP; changing the source/profile invalidates
      downstream Offline state instead of leaving an older package selectable.
- [x] Offline exposes byte progress and five workflow rows for validation,
      board-storage download, activation, arming and physical triggering;
      Activate/Arm/Test are gated by the exact downloaded/active package UUID.
- [x] FW Upload exposes five workflow rows with pending/in-progress/completed/
      failed states; completed rows are gray and stale bootloader selections are
      cleared by Return Bootloader or a new scan.
- [x] PLP `FW_ENTER_BOOTLOADER` validates payload/session/capability/busy state,
      invalidates the application CRC page before ACK, and resets only after
      response completion or bounded fallback.
- [x] PLP `RESET_PROGRAMMER` validates payload/session/capability/busy state,
      preserves the application CRC, ACKs before bounded reset, and the GUI
      closes the stale session after acknowledgement.
- [x] NuMaker-M032SE, M032 TEST BOARD and M484 use resident 16 KiB bootloaders
      plus fixed CRC-protected applications beginning at `0x4000`; USB/UART ISP
      cannot update boot code.
- [x] All boot projects use APROM flash algorithms (`M031_AP_128.FLM` and
      `M481_AP_512.FLM`), not LDROM. Initial provisioning requires
      `CONFIG0.CBS[7:6]=10` on both boards and `MBS bit 5=1` on M484 while
      preserving unrelated CONFIG bits; upload rejects a mismatch before erase.
- [x] The bridge updater shows the full HID path or COM name for every
      bootloader row and
      binds that device instance and the selected board profile to the reported
      application geometry before any erase command. PDID is probe/display
      information, not a hard-coded allowlist.
- [x] M032 TEST BOARD uses PF.2/PF.3 application PLP and UART ISP at
      115200 8-N-1, requires `MTB1` boot identity, uses PB.9 TIMER2/GPIO TX
      plus PB.10 GPIO-edge/TIMER2 software RX split TOOL0,
      PB.8 reset, PC.2 button and PA.11/PA.10 LEDs.
- [x] Each ISP program packet is verified only after `ReadData` completes, and
      both handoff directions mask interrupts, check ISP failure and confirm
      VECMAP readback before reset/branch. Bootloader FMC polling is bounded.
- [ ] ADR-0004 electrical buffer/HIL evidence is approved.
- [x] ADR-0003 single 64 KiB slot storage profile is approved and implemented.
- [x] Official-guide-derived Protocol B target driver is integrated.
- [x] M032 application links at `0x00004000` and ends before the reserved CRC
      page at `0x0000FA00`; bootloader, metadata and slot boundaries are
      independently enforced.
- [x] Formal Keil/uVision Release target generates AXF/HEX/BIN/map.
- [x] Nu-Link2-Me formal target selects M484SIDAE, 512 KiB APROM/160 KiB SRAM,
      HSUSBD, W25Q16, PB.12 NC/VDD sense disabled, KEY1 and four-LED adapters.
- [x] M032 PB.3/D1 is NC; its firmware and formal Keil target contain no
      target-VDD ADC setup or voltage-range interlock.
- [x] Nu-Link2-Me source target capability defaults fail-closed; the formal
      Keil target explicitly sets the gate to `1` as a physical-HIL candidate.
- [x] Embedded project follows repository-local `Library`/`SampleCode` layout
      and has no absolute external source/include paths.
- [ ] M032 image is flashed on physical hardware.
- [ ] Real RL78/G15 and G16 HIL programming is verified.
- [ ] Physical Nu-Link2-Me HSUSB/SPIM/button/LED bring-up is verified.
- [x] Physical Nu-Link2-Me HSUSB/W25Q16/TOOL0/reset/VDD G15 8 KiB online
      programming is verified for 20 warm runs and 10 bridge power-cycle runs.
- [ ] Physical Nu-Link2-Me KEY1 autonomous offline collision-recovery stability
      is verified. The original image completed 13/21 attempts, the removed
      nominal 2 ms-guard image completed 3/16, and the direct-GND SRAM-cache
      images completed 12/45 and later 34/45. The history-enabled image completed
      25/38 and correlated all 13 failures. After a complete bridge/target power
      cycle, the recovery-enabled image completed 64/64 normal transactions,
      but no collision retry was exercised; `HIL-NL2-011` remains open.
- [ ] Physical M032/M484 application-to-bootloader USB re-enumeration,
      interrupted application update recovery and valid-app launch are verified.
- [ ] Physical M032 TEST BOARD VCOM, TOOL0/RESET, online/offline G15/G16 and
      interrupted UART ISP recovery are verified.

## GitHub-readiness documentation verification - 2026-07-29

| Check | Result |
| --- | --- |
| Root first-time guide | PASS - English-first/Traditional-Chinese guide covers both primary boards, exact two-branch 47-ohm TOOL0 wiring, VDD/RESET/GND, all five GUI tabs and clean behavior |
| GUI screenshots | PASS - Program, Offline and FW Upload images use neutral example paths; original path-bearing JPG files were removed |
| Publish-source privacy scan | PASS - 683 source text files checked; zero selected workstation paths, user/customer names, private-key markers or common token patterns |
| Markdown structure | PASS - 66 Markdown files, zero broken local links, zero unpaired fences; all six normative documents present |
| Native Release regression | PASS - build completed with 0 warnings/errors; 243/243 checks passed after converting the validated-image source test to relative fixture paths |
| Root build cleanup | PARTIAL - the cleaner is intentionally limited to `build/obj` and `build/logs`; objects were removed, while one active GUI process kept its ignored session log open and the cleaner correctly returned failure with close-and-retry guidance |

## M032 TEST BOARD implementation verification - 2026-07-23

| Check | Result |
| --- | --- |
| Schematic mapping note | PASS - ADR-0012 and `M032_TEST_BOARD_WIRING.md` record A11/PB.10, A10/PB.9, A9/PB.8 and PB.11/TH |
| PC transport boundary | PASS - SDK owns Win32 COM; GUI selects `M032 TEST BOARD` and uses the same Program/Offline/History/Log flows |
| UART PLP framing | PASS - exact 64-byte CRC-valid frame, fragmented input, leading noise and bad-CRC resynchronization native tests |
| UART ISP identity/range | PASS - `MTB1` plus exact `0x4000/0xBC00/0xFC00/0xFBFC` geometry required before erase |
| APROM storage | PASS - metadata `0xFC00-0xFFFF` plus slot A `0x10000-0x1FFFF`; 64 KiB maximum |
| Native Debug x64 | PASS - 239/239 checks |
| Native Release x64 | PASS - 239/239 checks |
| PC MFC Debug x64 | PASS - 0 warnings, 0 errors |
| PC MFC Release x64 | PASS - clean Rebuild, 0 warnings, 0 errors; final root EXE rebuilt as Release |
| M032 TEST BOARD UART bootloader Keil Release | SUPERSEDED - see the current correction results below |
| M032 TEST BOARD application Keil Release | SUPERSEDED - firmware `1.0.0.2`, 19.20 KiB, 27.30 KiB linkable free; fixed `0xBC00` BIN; CRC32 `0xE6643DEC`; 0 warnings/errors |
| Existing NuMaker-M032SE Keil Release regression | PASS - boot 8.13 KiB, app 19.02 KiB; 0 warnings/errors |
| Existing Nu-Link2-Me M484 Keil Release regression | PASS - boot 4.53 KiB, app 24.08 KiB; 0 warnings/errors |
| Physical M032 TEST BOARD HIL | NOT RUN - external VCOM and target hardware were not controlled by this implementation session |

## M032 TEST BOARD UART handoff correction - 2026-07-24

| Check | Result |
| --- | --- |
| Reported first-board symptom | CONFIRMED - application and NuvoISP reads ended in `UART_IO_TIMEOUT (0x21107004)` before erase; log contained no successful Return Bootloader acknowledgement |
| Win32 COM partial-read handling | PASS (software/build) - COM reads now return buffered partial data and assemble the exact 64-byte PLP/NuvoISP response under the outer deadline |
| Application-to-ISP COM ownership | PASS (software/build) - GUI closes the Application transport after the handoff ACK, including the error cleanup path |
| UART bootloader discovery | PASS (software/build) - OS COM enumeration no longer completes Scan; only a bounded NuvoISP probe with `MTB1` and exact geometry is listed |
| UART response completion | PASS (software/Keil) - application transport remains busy until UART0 `TXEND`, not merely until the final bytes enter the FIFO |
| Updated physical Application connect / Return / Scan / Upload | PASS (user HIL) - UART ISP upload completed and firmware `1.0.0.2` reconnected over the Application PLP session |

## M032 TEST BOARD internal APROM staging correction - 2026-07-24

| Check | Result |
| --- | --- |
| Reported physical symptom | CONFIRMED - firmware `1.0.0.2` accepted the UART PLP session and validated the G15 image, then rejected `IMAGE_BEGIN` with `STORAGE_WRITE_FAILED (0x20605003)` at 0% load; target programming had not started |
| Package slot address audit | PASS - metadata maps to `0xFC00-0xFFFF`; backend slot offset `0x400` maps the first OIP erase to APROM `0x10000`; 7,638-byte OIP rounds to fifteen 512-byte pages and remains inside the 64 KiB slot |
| FMC operation hardening | PASS (software/Keil) - every erase/program clears stale `ISPFF`, reasserts `ISPEN/APUEN`, uses a bounded interrupt-masked command window and performs read-back verification |
| Stack partition | PASS (linker) - long-lived main objects moved to static RAM and Keil stack reservation increased from 512 bytes to 4 KiB |
| Portable firmware strict C90 | PASS - 14 portable files and 11 M032 NuMicro adapters |
| Physical OIP staging with firmware 1.0.0.3 | PASS/PARTIAL (user HIL) - GUI reached exactly 25% with all 7.46 KiB loaded; the earlier `STORAGE_WRITE_FAILED` did not recur, then target connection failed before erase with `DRIVER_TARGET_STATUS (0x20806003), detail=0` |
| UART ISP Run Application audit | PASS (software/Keil), HIL PENDING - corrected resident bootloader clears stale FMC status, validates CRC, performs a full-chip reset, verifies CRC again on clean boot and maps APP `0x4000`; PC UART drains the complete no-response command before COM close; requires one ICP/debugger factory-HEX update |
| TOOL0 scheduling and diagnostics | PASS (software/build/HIL) - PB.9 software TX and PB.10 GPIO-edge/TIMER2 software RX use IRQ priority 0 ahead of PC UART0 priority 3; RX is disabled during entry/TX and armed at the first stop bit; the validated falling edge directly starts one periodic 417-tick data-sampling schedule; only PB.9-Low/PB.10-High during a transmit Low maps to direction collision; software parity/stop/FIFO faults report `TARGET_LINK_UART_FRAMING`; UART4 and echo filtering are absent; deterministic polling TX completed the repeated G15 Online/Offline captures |
| M032 TEST BOARD timeout-clock correction | PASS (source/build) - `CLK_SysTickDelay()` was removed from the test-board application; HIRC48 TIMER1 now supplies polling one-shot microsecond entry delays while SysTick continues Protocol B deadlines; build script rejects regression to the SysTick helper |
| Firmware 1.0.0.5 physical diagnosis | PASS/PARTIAL - SysTick correction eliminated the indefinite 25% wait; three bounded TARGET_CONNECT attempts terminated in 1004 ms, all with `TARGET_LINK_UART_FRAMING`, before erase |
| Firmware 1.0.0.6 physical diagnosis | PASS/PARTIAL - the state machine made three bounded TARGET_CONNECT attempts in 1003 ms, then ended before erase with `TARGET_LINK_DIRECTION_COLLISION`; source audit found byte-level non-echo target data was being classified as collision |
| Firmware 1.0.0.7 physical diagnosis | PASS/PARTIAL - all three attempts transmitted `0x3A` and G15 returned valid `0x36`; the LA also showed an entry-hold parity-error `0x00`, and the bridge terminated with `DRIVER_TARGET_STATUS detail=0` before the valid response completed |
| Firmware 1.0.0.8 physical diagnosis | PASS/PARTIAL - all three attempts again transmitted `0x3A` and received valid `0x36`, but the bridge still returned stale status `0x00`; measured release-to-first-TX was about 53 us, shorter than one complete 95.5 us 115200 8-O-1 frame |
| Firmware 1.0.0.10 physical diagnosis | PASS/PARTIAL - all three bounded attempts transmitted valid `0x3A` and received valid G15 `0x36` according to the 115200 8-O-1 TOOL0 decoder, but the bridge reported `TARGET_LINK_UART_FRAMING`; the M484-derived half-bit interrupt plus timer restart accumulated two Cortex-M0 scheduling delays before the first sample |
| M032 TEST BOARD software RX phase correction | PASS (source/C90/link HIL) - firmware `1.0.0.11` uses `GPIO_PAPB_IRQn/GPABGH_IRQHandler` to validate the PB.10 falling edge and directly starts one periodic HIRC48 417-tick schedule whose first interrupt samples data bit 0; physical HIL completed entry, erase, all 2,048 program-unit ACKs and final verify ACK |
| Firmware 1.0.0.11 physical image diagnosis | FAIL/ROOT CAUSE FOUND - `decoder--260727-101821.csv` contains the complete 8 KiB transaction, but all 8,192 programming payload bytes are `0xFF`; after attempt preparation called `SYS_LockReg()`, storage `FMC_Read()` could not trigger protected `ISPTRG` and returned stale `ISPDAT=0xFFFFFFFF`, so the prior target ACK-only PASS was false |
| APROM package-read and PASS hardening | PASS (software/build/HIL) - Package-slot reads use memory-mapped APROM access; Protocol B compares the pre-entry exact-profile dense-image CRC with CRC accumulated from every post-prepare programming unit and fails `INTEGRITY_CRC` on a changed provider; physical `1.0.0.12` captures contained the exact non-`FF` P20/P21 image and complete 8 KiB transactions |
| Firmware 1.0.0.12 repeated-programming diagnosis | FAIL/ROOT CAUSE FOUND - `Rl78ProgrammerTool_20260727_105342_396_32388.log` passed 5/9 warm jobs; `decoder--260727-105439.csv` captured failures at units `0x067B`, `0x0002` and `0x058F`, each with PA.9/TOOL0 delayed by one 8.7 us bit period; two PC UART 64-byte buffer copies used PRIMASK and could suppress one TIMER2 overflow during status polling |
| PC UART / TOOL0 interrupt isolation | PASS (software/build), HIL PARTIAL - firmware `1.0.0.13` masks only `UART02_IRQn` around report buffers, never globally disables interrupts; Keil guard rejects `__disable_irq()`/PRIMASK in the adapter; subsequent HIL proved this necessary but insufficient |
| Intended-1.0.0.13 image-switch and TX diagnosis | FAIL/ROOT CAUSE FOUND - the 12-run PC log used the exact P21 UUID for jobs 1-7 and exact P20 UUID for jobs 8-12, ruling out stale GUI package selection; LA payload comparison found bit-7 loss `A9 -> 09` at P21 offset `0x0B3A` in a reported PASS and `A8 -> 08` at P20 offset `0x12B6` before target NAK, proving periodic ISR TX can silently corrupt a parity-compatible byte; the legacy log did not record reported firmware identity, so the board version is not independently proven by that file |
| Deterministic M032 TEST BOARD TX | PASS (software/build/HIL) - firmware `1.0.0.14` polls TIMER2 for start/data/parity within a bounded approximately 87 us PRIMASK interval, restores interrupts and arms PB.10 RX at the first stop bit, and clears stale TIMER2 NVIC pending state; Keil guard requires this path; captured G15 8 KiB Online programming passed 20/20 with exact payloads and final verify ACKs |
| Additional seven-run Online capture | PASS (user HIL) - `decoder--260727-131239.csv` contains seven complete transactions: three exact P21 and four exact P20 payloads, each with all 2,048 four-byte units, zero non-ACK unit responses and a final verify ACK |
| PC.2 Offline capture | PASS (user HIL) - `decoder--260727-131515.csv` contains 22/22 complete P21 transactions, each with the exact 8,192-byte payload, 2,048 unit ACKs and final verify ACK; the one entry `0x00` parity marker per job is outside programmed data |
| M032 TEST BOARD Application LED policy | PASS (source/build), PHYSICAL HIL PENDING - firmware `1.0.0.15` uses Red 125/125 ms blink for both Online and Offline programming, solid Red for FAIL/CANCELLED/interrupted/outcome unknown, solid Green for PASS and slow Green for idle; Keil build guard enforces the mapping |
| GUI validated-source binding | PASS - parsed image/OIP is bound to the exact displayed firmware path; edit/Browse/profile change clears it, the path control is disabled while busy, and a stale asynchronous validation completion is rejected; four native regression checks pass |
| GUI connection-version provenance | PASS - a successful Connect now logs reported product, firmware version and uptime instead of uptime alone; MFC Debug/Release build with `/W4 /WX` |
| M032 TEST BOARD application Keil Release | PASS - firmware `1.0.0.15`, 20.49 KiB, 26.01 KiB linkable free; fixed `0xBC00` BIN; CRC32 `0x0C02F625`; SHA-256 `2F9B099E5A6C0612E8908A15A723DF819AD233B5CCF106788EF18DCB7350451D`; 0 warnings/errors |
| M032 TEST BOARD UART bootloader Keil Release | PASS - 5.42 KiB, 10.58 KiB free; 0 warnings/errors |
| Native Debug/Release x64 | PASS - 243/243 checks in each configuration, including changed-image false-PASS rejection and exact GUI source-path binding; 0 warnings/errors |
| MFC Debug/Release x64 | PASS - sequential builds, 0 warnings/errors; final root EXE is Release. An initial parallel Debug link collided with the shared Release output path and was rerun sequentially |
| Documentation structure | PASS - 67 Markdown files, 0 broken local links, 0 unclosed Mermaid fences and all six normative documents present |
| Physical G15 online programming | PASS - deterministic firmware `1.0.0.14` completed 20/20 captured G15 8 KiB jobs; all 8,192-byte wire payloads matched the selected dense P20/P21 image, all 2,048 units received ACK and final verify ACK was present |

## Nu-Link2-Me TOOL0 turnaround verification - 2026-07-20 through 2026-07-22

| Check | Result |
| --- | --- |
| User HIL with prior bridge BIN | PARTIAL/FAIL - both 47-ohm branches, stable VDD and RESET/TOOL0 entry activity confirmed; `Program Online` ended at `TARGET_CONNECT` with `TARGET_LINK_TIMEOUT` before erase |
| Official Protocol B timing review | PASS - 115200-bps 8-O-2 TX/8-O-1 RX, `tSU` minimum 10 us, `tHD` minimum 1 ms, `tSUINIT` maximum 100 ms and target `tDRT` maximum 1 us confirmed from the controlled Renesas guide |
| PA.9-to-PA.10 turnaround correction | PASS - falling target data during the released stop interval now transfers TIMER2 from TX bookkeeping to RX start-bit confirmation instead of being discarded by `tx_busy` |
| Pre-guard autonomous KEY1 capture | PARTIAL/FAIL - 13/21 complete: 8/13 with PA.9/PA.10 pre-resistor probes and 5/8 without them; similar proportions do not implicate probe loading |
| Failed-attempt direction evidence | FAIL - every observable failed first-half attempt had PA.9 low while PA.10/TOOL0 stayed high for 0.15-1.84 ms, consistent with an open TX path or high-drive contention |
| Nominal 2 ms post-ACK capture | PARTIAL/FAIL - 3/16 complete; 12 stopped after program units 37-244 and one during mode entry; measured ACK-to-next-data was normally 1.51-1.55 ms, so the fixed guard was removed |
| TX echo detection and SRAM cache | PASS (software/build) - PA.9 low with PA.10 high reports `TARGET_LINK_DIRECTION_COLLISION (0x20907003)`; the exact 64 KiB-or-smaller code image is UUID/profile/size/CRC-bound and preloaded before target RESET |
| SRAM-only program-unit reads | PASS (native) - after one preload, all 2,048 four-byte reads completed with no additional backing-storage read; corruption forced authoritative reload |
| Direct-GND SRAM-cache HIL | PARTIAL/FAIL - first capture completed 12/45; the later 116.78-second no-probe capture completed 34/45, with seven exact-prefix truncations and four corrupted sequences; PA.9/PA.10 were not captured |
| Per-attempt clean state | PASS (software/build) - every Protocol B entry releases PA.9, resets TIMER2, clears GPIO/TIMER/NVIC state and RX FIFO/error before asserting the target entry sequence; per-job retry does not reset the whole M484 |
| Offline failure history | PASS/PARTIAL - native/build checks pass; physical 25/38 run retained 13/13 failures with `dropped=0`, and job counters exactly matched every LA truncation; programming stability still failed |
| Bounded offline collision recovery | PASS/PARTIAL - software/build checks pass; persistent `TARGET_CONNECT` Green retains one-second pre-erase retry; terminal failures immediately park TOOL0/G15 RESET; only exact direction collision starts a full job after one second, with two automatic retries per KEY1; a post-power-cycle run completed 64/64 normal transactions, but forced collision retry was not exercised |
| Nu-Link2-Me full-chip `RESET MCU` | PASS (software/build), NOT RUN (physical) - firmware `1.0.1.5` ACKs then resets the full M484 to clear internal GPIO/TIMER/SPIM/HSUSBD state, preserves application CRC and returns through APROM0; external W25Q16/target remain powered; re-enumeration and subsequent KEY1 HIL are pending |
| Nu-Link2-Me canonical mirror | PASS - 72/72 C/H files synchronized and checked |
| Nu-Link2-Me Keil Release | PASS - boot 4.53 KiB, app 24.08 KiB; both 0 warnings/errors; APP BIN `0x7C000`, CRC32 `0xAC381304`; total RW/ZI 73,528 bytes including 2,048-byte stack |
| Corrected bridge BIN physical ACK/erase/program/verify | PASS - 20/20 warm and 10/10 bridge power-cycle G15 8 KiB programming runs; each captured transaction has 2,048 four-byte data ACKs and final verify ACK |
| Immediate KEY1 handshake restart | PASS (software/build) - a debounced KEY1 during autonomous pre-erase `TARGET_CONNECT` safely bypasses the remaining 500 ms wait in the same job; destructive stages reject restart; physical HIL pending |
| Native and MFC builds | PASS - native Debug/Release 239/239; prior MFC Debug/Release 0 warnings/errors and final root EXE remains Release; MFC was not rebuilt for the firmware-only `1.0.0.12` change |
| Documentation structure | PASS - 66 Markdown files, 0 broken local links, 0 unclosed Mermaid fences, all six normative documents present at `1.0-draft` |

## Latest Offline GUI/bridge reset verification - 2026-07-23

| Check | Result |
| --- | --- |
| Shared Program/Offline profile and MOT/HEX/BIN source | PASS - one control set and one validated package prevent cross-tab source divergence |
| Automatic OIP generation and optional export | PASS - existing content-detected parser/OIP builder is used by Load + Validate; Save OIP is not an input prerequisite |
| Five Offline workflow rows and stale-state invalidation | PASS - source/profile changes clear download/activate/arm/trigger state; completed rows use the common gray treatment |
| Exact package gating | PASS - Activate requires the session-downloaded UUID; Arm/Test require the bridge active UUID to match the current OIP |
| Offline download progress | PASS - package bytes and SDK transfer basis points drive loaded/total percentage and KiB |
| MFC Debug x64, MSVC v143, `/W4 /WX` | PASS - 0 warnings, 0 errors |
| MFC Release x64, MSVC v143, `/W4 /WX` | PASS - clean Rebuild, 0 warnings, 0 errors; final root EXE is Release |
| Native Debug/Release x64 | PASS - 222/222 checks in the current configuration |
| Connected Nu-Link2-Me visual/runtime Offline workflow | PARTIAL/PASS - SPI download/Activate/Arm/KEY1 ran physically; historical completion was 13/21, 3/16, 12/45, 34/45, 25/38 and 9/28. After a full bridge/target power cycle, the current recovery image completed 64/64 normal G15 transactions; forced collision retry and the new GUI full-chip-reset path remain pending |

## Latest GUI workflow verification - 2026-07-17

| Check | Result |
| --- | --- |
| MFC Debug x64, MSVC v143, `/W4 /WX` | PASS - 0 warnings, 0 errors |
| MFC Release x64, MSVC v143, `/W4 /WX` | PASS - 0 warnings, 0 errors |
| Native Debug x64 | PASS - 172/172 checks in the earlier 2026-07-17 run; not rerun for this window-only change |
| Native Release x64 | PASS - 172/172 checks in the earlier 2026-07-17 run; not rerun for this window-only change |
| Physical FW Upload five-row workflow and completed-row gray rendering | NOT RUN - no connected board/interactive visual session in this verification |
| Maximized initial main-frame source/build integration | PASS - initial display uses `SW_SHOWMAXIMIZED`; MFC Debug/Release pass |
| Maximized initial-window interactive visual confirmation | NOT RUN - source integration and MFC builds are verified below; no interactive desktop launch in this verification |
| Documentation structure | PASS - 64 Markdown files checked, 0 broken local links, 0 unclosed Mermaid fences, all six normative documents present at `1.0-draft` |

## Full baseline verification - 2026-07-14

| Check | Result |
| --- | --- |
| MFC Debug x64, MSVC v143, `/W4 /WX` | PASS - 0 warnings, 0 errors |
| MFC Release x64, MSVC v143, `/W4 /WX` | PASS - 0 warnings, 0 errors |
| Program/FW Upload progress and next-step UI on physical HID hardware | NOT RUN - source integration and both MFC builds pass; connected-board visual/runtime confirmation remains pending |
| Root `build` layout after `clean_mfc.bat` | PASS - only `Rl78ProgrammerTool.exe` and `rl78_programmer.ini`; preserved SHA-256 hashes unchanged |
| Optional-log INI lifecycle, isolated temporary EXE | PASS - missing key restored unchecked; enable wrote `SaveLogToFile=1` and created one log; relaunch restored checked and created a second unique log; disable wrote `SaveLogToFile=0` |
| Native Debug x64 | PASS - 172/172 checks |
| Native Release x64 | PASS - 172/172 checks |
| Portable firmware, armclang `-std=c90 -pedantic -Werror` | PASS - 14 portable C files |
| M032 NuMicro/CMSIS adapters, armclang `gnu90` | PASS - 6 adapter C files |
| M484 adapters and both vendor-derived ISP_HID projects | PASS - verified by their Keil Release builds; no separate strict-`-std=c90` claim |
| Physical M032 USB enumeration | NOT RUN - no board connected/authorized |
| Physical M032 GPIO/UART/PB.3-NC measurement | NOT RUN - no board/fixture connected |
| M032 resident bootloader Keil/uVision Release | PASS - 8.13 KiB, 7.87 KiB free; 0 warnings/errors; AXF/HEX/BIN/map generated |
| M032 application Keil/uVision Release | PASS - 18.81 KiB, 27.69 KiB linkable free; fixed APP BIN `0xBC00`, CRC32 `0x50A01C63` at `0x0000FBFC`, factory BIN size `0xFC00` ending before metadata at `0x0000FC00`; ZI 4,448 bytes including 512-byte stack; 0 warnings/errors |
| Superseded pre-correction Nu-Link2-Me build | PASS - 0 warnings/errors; 17.73 KiB ROM, 494.27 KiB free; AXF/HEX/BIN/map generated before MCU correction |
| Nu-Link2-Me M484SIDAE resident bootloader Keil/uVision Release | PASS - 4.53 KiB, 11.47 KiB free; 0 warnings/errors; AXF/HEX/BIN/map generated |
| Nu-Link2-Me M484SIDAE application Keil/uVision Release | PASS - 24.08 KiB, 467.92 KiB linkable free; fixed APP BIN `0x7C000`, CRC32 `0xAC381304` at `0x0007FFFC`, factory BIN `0x80000`; total RW/ZI 73,528 bytes including 2,048-byte stack; 0 warnings/errors |
| Nu-Link2-Me target capability gate | PASS - formal target defines `NULINK2ME_RL78_TARGET_APPROVED=1`; source default remains `0` |
| Nu-Link2-Me PB.12 policy | PASS - board profile declares VDD sense disabled, contains no PB.12 assignment/EADC setup and passes optional-interlock native tests |
| Nu-Link2-Me schematic TOOL0 audit | PASS - PDF page 1 maps PA.9/`SWDH_DAT` to ICEJ4-3 and PA.10/`SWDH_CLK` to ICEJ4-5 with no onboard series resistors; physical HIL passed with two external 47-ohm branches plus a 1-kohm TOOL0-to-3.3-V pull-up |
| Nu-Link2-Me GPIO software UART | PARTIAL/PASS - online HIL passed 20 warm and 10 power-cycle G15 runs; historical KEY1 results were 13/21, 3/16, 12/45, 34/45, 25/38 and 9/28; after a complete power cycle the recovery image completed 64/64 normal transactions, but forced collision retry remains pending |
| M032 PB.3 policy | PASS - board profile declares VDD sense disabled; active board source/project contain no PB.3 assignment, ADC setup or `adc.c` member; native tests pass |
| Nu-Link2-Me physical HSUSB/SPIM/KEY1/LED | PARTIAL/PASS - HSUSB, W25Q16 and KEY1 path ran; historical stability was 13/21, 3/16, 12/45, 34/45, 25/38 and 9/28, followed by 64/64 normal transactions after a complete power cycle; forced recovery, power-cut, full-chip reset and LED-specific acceptance remain pending |
| Nu-Link2-Me physical PB.12 NC state/TOOL0/reset/RL78 programming | PARTIAL/PASS - TOOL0/reset/VDD and online G15 8 KiB passed 20 warm and 10 bridge power-cycle runs; offline normal operation most recently passed 64/64 after full bridge/target power; PB.12 NC measurement, forced collision recovery, GUI full-chip reset and G16 remain pending |
| M032 BSP/SampleCode structure | PASS - 63/63 canonical C/H mirror files; relative project structure retained |
| Nu-Link2-Me BSP/SampleCode structure | PASS - 63/63 canonical C/H mirror files; relative project structure retained |
| M032 physical flash | NOT RUN - no board connected/authorized |
| Physical M032 application/ISP HID transition and update interruption | NOT RUN - no board connected/authorized |
| Physical Nu-Link2-Me application/ISP HSUSB transition and update interruption | NOT RUN - no board connected/authorized |
| RL78 physical erase/program/verify | PARTIAL/PASS - Nu-Link2-Me to G15 8 KiB passed; M032 and G16 physical programming remain pending |

The 222 native checks cover CRC16, CRC32, SHA-256, PLP golden frames and padding,
S-record parsing, OIP validation/corruption, firmware negotiation/session,
profile-driven extended address maps, fail-closed target access, signature
callback dispatch, Online Only/Offline Armed HID query/switch/persistence,
offline authoritative PASS, external-reset gating, HID suppression, autorun
grace period, exact CLOSE_SESSION/transport-reset cleanup, stale-session KEY1
reclaim, host-versus-autonomous job ownership, bounded offline failure-history
retention/overflow, the 64 KiB single-slot
capacity boundary, interrupted-replacement fail-closed recovery, integrity
validation, journaled activation and reboot recovery, M032 HID
OUT/IN queues, pin conflict validation, safe initialization order, official
TOOL0/reset timing, safe release, disabled sensing on both current boards and
the reusable optional target-VDD sense policy, OIP target
address reads, all four G15/G16 Protocol B sizes, 500 ms retry, exact-size
mismatch, erase/write/verify status mapping, destructive timeout
`OUTCOME_UNKNOWN`, deferred safe-point cancellation and Data Flash rejection,
PB.14 active-low ownership and the ready/program/PASS/FAIL indicator patterns.
They also cover preservation of the default M032 GET_DEVICE_INFO identity,
custom Nu-Link2-Me identity pagination, and the audited M484 TOOL0/reset/KEY1/
SPIM/PB.12-NC pin profile. Nu-Link2-Me recovery checks cover the one-second
connect timer, immediate terminal park, two collision-only full-job retries,
budget exhaustion, non-collision/unknown/cancel fail-closed behavior,
pending-action serialization, and normal-programming/online-job exclusion. A composed software flow covers GUI-equivalent W25Q16
download/finalize, online candidate programming, activate/arm, KEY1 active-image
programming and authoritative PASS; it does not replace physical HIL.

Additional image-parser coverage exercises strict S-record/Intel HEX content
detection, including a colon-prefixed `.mot` plus corrupt/checksum/EOF/overlap/
address cases, without cross-format fallback after validation failure.

The repository `RL78_G15_Simple_GPIO_P20.mot` and
`RL78_G15_Simple_GPIO_P21.mot` fixtures are detected as Intel HEX and validate
as 6,402 data bytes each for the G15 8 KiB profile. Their addressed image is
correctly rejected by the G15 4 KiB profile.

The added updater checks cover the `0x0506` allocation, non-empty payload,
missing negotiation/session/capability/callback, active transfer/job and invalid
state rejection, successful prepare/UPDATING response, M032/M484 fixed image
profiles, valid image CRC/additive checksum, wrong size/CRC rejection, full
bootloader HID-path presentation, selected-profile/reported-application-
geometry rejection before erase, four M032/M484 CBS/MBS CONFIG cases,
no-invalidation rejection of incompatible handoff CONFIG, post-`ReadData` byte
comparison, interrupt-masked VECMAP handoff and the Nuvoton ISP application-
start/size packet contract.

Documentation check rerun after the build-output cleanup: 64
Markdown files, zero broken local links, zero unpaired fences, zero invalid
UTF-8 files, zero merge-conflict-marker files, and zero UTF-8 replacement
characters in the English/Traditional-Chinese root README. The firmware C/H
non-ASCII result was not rerun as part of this documentation-only pass.

## Reproduction

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_native_tests.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\scripts\build_native_tests.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\scripts\build_mfc.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\scripts\build_mfc.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\scripts\build_firmware_core.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\sync_m032_sample.ps1 -Check
powershell -ExecutionPolicy Bypass -File .\scripts\build_m032_keil.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\sync_nulink2me_sample.ps1 -Check
powershell -ExecutionPolicy Bypass -File .\scripts\build_nulink2me_keil.ps1
.\scripts\clean_mfc.bat
```

Architecture source checks from the baseline remain applicable: all six
normative specifications, contract registry, ADR index, traceability and test
strategy are present; PLP and OIP golden vectors remain covered by native tests.
