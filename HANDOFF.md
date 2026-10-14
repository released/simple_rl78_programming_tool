# Handoff

## Current status

- Directory/repository naming (2026-10-14): canonical name is
  `simple_rl78_programming_tool`. Original GitHub repository was renamed in
  place (ID1315625250, public/main unchanged) and origin now uses the new URL.
  Local directory rename is pending: both normal and native directory moves
  were denied by Windows. Do not claim the local path has moved yet.
  Existing untracked build/logs are preserved and excluded from this commit.
  Project/EXE/INI names and protocol behavior remain unchanged. Post-move
  build/tests and rename-document commit/push still require verification.

- GitHub-readiness pass (2026-07-29): the root README is now a bilingual
  first-time user guide centered on Nu-Link2-Me and M032 TEST BOARD. It contains
  exact two-branch 47-ohm TOOL0 wiring, target VDD/RESET/GND connections,
  Program/Offline/FW Upload procedures, sanitized GUI screenshots and the
  documented `clean_mfc.bat` scope. The cleaner removes only root
  `build/obj` and `build/logs`; with the expected layout this leaves the GUI
  EXE and portable INI. Private workstation/customer source notes,
  user-specific uVision state and path-bearing diagnostic files were removed
  or de-identified. A scan of 683 publish-source text files found zero selected
  local-path, user/customer-name, private-key or common token patterns; 66
  Markdown files have zero broken local links and zero unpaired fences.
  `build/`, Keil `obj/lst`, IDE state and capture files remain ignored.
  One GUI session log was still locked by the running application during the
  final clean attempt; the revised cleaner reports this as incomplete and the
  ignored file cannot enter Git. Close the GUI and rerun the cleaner before
  packaging a non-Git archive.
- Phase: implementation, first executable vertical slice.
- Contract baseline: `1.0-draft`.
- PC: MFC GUI, HID and Win32 COM transports, SDK, strict content-detected Motorola S-record/
  Intel HEX plus explicit raw BIN ingestion, OIP builder and
  board-selected `FW Upload (USB HID/UART ISP)` bridge application updater
  implemented. Optional
  UTF-8 diagnostic file logging is available without changing the always-on
  in-memory Log/History views. Program and Offline now share one exact-profile
  target-image input; Offline builds OIP automatically and guides storage
  download, activation, arming and physical triggering through five rows.
- Firmware: portable PLP/programming core, single-slot package/mode journal,
  G15/G16 Protocol B state machine, application composition, NuMicro HID/FMC
  adapters, resident bootloader handoff and accepted NuMaker-M032SE UART1/reset/
  PB.3-NC profile implemented.
- M032 TEST BOARD: ADR-0012 records the corrected schematic mapping
  A11/PB.10, A10/PB.9, A9/PB.8 RESET and PB.11/TH. The application uses
  PF.2/PF.3 UART0 PLP at 115200 8-N-1, PB.9 TIMER2/GPIO software TX plus
  PB.10 GPIO-edge/TIMER2 software RX for RL78
  TOOL0, PC.2 for offline/forced ISP, PA.11/PA.10 LEDs and the existing
  internal-APROM 64 KiB slot. Its resident UART ISP requires `MTB1` identity
  and exact geometry before application erase. Software/native/Keil builds
  pass. Physical VCOM/UART ISP and TOOL0/RESET waveform capture have been run;
  the G15 link now completes the full 8 KiB transaction, the discovered APROM
  package-read defect was corrected in `1.0.0.12`; `1.0.0.13` isolated the PC
  UART but a 12-run capture still found two bit-7 losses, including one
  acknowledged corrupt byte. Deterministic polling TX in `1.0.0.14` then
  passed 20/20 captured G15 8 KiB Online jobs and 22/22 captured PC.2 Offline
  jobs with exact wire payloads and final verify ACKs. Firmware `1.0.0.15`
  retains that link path and changes the common Online/Offline Application
  indication to blinking Red while programming, solid Red on failure and
  solid Green on PASS. G16, power-interruption and physical LED acceptance
  remain open.
  A 2026-07-24 first-board run exposed that the Win32 COM adapter requested
  256 bytes while the device returned one 64-byte report, and that UART
  bootloader scan treated every OS COM port as a candidate. The current build
  uses partial-read completion, releases the application COM handle after
  `FW_ENTER_BOOTLOADER`, keeps firmware TX busy through UART0 `TXEND`, and
  lists only a bounded NuvoISP probe that reports `MTB1`. Physical rerun with
  firmware `1.0.0.2` confirmed UART ISP application upload and Application
  reconnect. Its first online G15 attempt then failed at `IMAGE_BEGIN` with
  `STORAGE_WRITE_FAILED`, before any target-link activity. Firmware `1.0.0.3`
  clears stale FMC failure state before each command, reasserts
  `ISPEN/APUEN`, executes erase/program in bounded critical sections and
  verifies erased pages/programmed words. The application stack reservation is
  now 4 KiB and long-lived main objects use static RAM. Physical package
  download with `1.0.0.3` then passed: the GUI reached 25%, proving the full
  OIP was staged in internal APROM. The job next failed before erase at
  `TARGET_CONNECT` with `DRIVER_TARGET_STATUS (0x20806003), detail=0`.
  Firmware `1.0.0.4` gives TOOL0 GPIO/TIMER2 priority over the PC UART0 ISR,
  classifies parity/stop/FIFO faults as `TARGET_LINK_UART_FRAMING`, and permits
  three bounded complete Online handshakes before erase. The GUI now displays
  terminal stage/error/detail/attempt instead of only a generic failure.
  The resident UART bootloader also clears stale FMC failure state, validates
  the APP CRC and uses a full-chip reset for `Run Application`; the clean boot
  verifies the CRC again and maps vectors to `0x4000`.
  Because UART ISP never updates the resident `0x0000-0x3FFF` region, the user
  must provision the new factory HEX once to test this correction. The
  subsequent `1.0.0.4` G15 run remained at `TARGET_CONNECT`/25% indefinitely.
  Source audit found that the M032 `DelayUs()` called `CLK_SysTickDelay()`,
  whose completion disables the SysTick used by all Protocol B deadlines.
  Firmware `1.0.0.5` reserves HIRC48 TIMER1 for polling one-shot microsecond
  entry delays and leaves SysTick running, matching Nu-Link2-Me's independent
  DWT/SysTick design. Physical G15 rerun with `1.0.0.5` confirmed that the
  bounded state machine no longer hangs: all three attempts completed in
  1004 ms, but each failed on software-RX framing before erase. Firmware
  `1.0.0.6` swapped roles without changing the two-47-ohm merged wiring: PB.9
  remains TIMER2/GPIO transmit and PB.10 uses its native UART4_RXD function.
  Physical G15 testing then completed three bounded attempts in 1003 ms, but
  each ended before erase because the first non-matching UART4 byte was
  classified as direction collision. Firmware `1.0.0.7` keeps the physical
  PB.9-Low/PB.10-High bit check as the collision authority, removes only a
  matching self-echo prefix and returns the first non-matching byte as the
  target response. Physical G15 testing with `1.0.0.7` showed three valid
  `0x3A -> 0x36` exchanges, but all ended with `DRIVER_TARGET_STATUS detail=0`
  because the entry hold's invalid `0x00` completed after the release-edge RX
  reset. Physical testing with firmware `1.0.0.8` again produced three valid
  `0x3A -> 0x36` exchanges but returned detail zero: its purge was only about
  53 us after release, shorter than one 95.5 us 8-O-1 frame. Firmware
  `1.0.0.9` added a released-line guard, but the user selected the more direct
  Nu-Link2-Me design before that mitigation was rerun. Firmware `1.0.0.10`
  removes UART4 and echo filtering. PB.10 GPIO falling-edge detection is
  disabled during entry/TX, armed at the first transmit stop bit and sampled
  through the shared TIMER2 RX states. Physical G15 testing with `1.0.0.10`
  completed three bounded attempts: the analyzer decoded valid `0x3A -> 0x36`
  every time, but the bridge reported `TARGET_LINK_UART_FRAMING`. Timing audit
  showed that copying the M484 half-bit confirmation interrupt plus full-bit
  timer restart accumulated two scheduling latencies on the 48 MHz Cortex-M0
  and placed the first internal data sample too late. Firmware `1.0.0.11`
  validates the PB.10 falling edge in the GPIO handler and starts one periodic
  417-tick schedule whose first interrupt samples data bit 0. Its physical G15
  rerun completed all 2,048 four-byte writes and final verify ACK, proving the
  corrected receive phase, but all 8,192 transmitted payload bytes were
  `0xFF`. The package header had been opened before SYS lock; subsequent
  `FMC_Read()` calls attempted a protected `ISPTRG` command after TOOL0
  preparation restored SYS protection and returned stale
  `ISPDAT=0xFFFFFFFF`. Firmware `1.0.0.12` reads Package-slot data through
  memory-mapped APROM access. Protocol B now also compares a pre-entry
  exact-profile dense-image CRC with a CRC accumulated from the post-prepare
  four-byte programming units, rejecting any changed/stale provider with
  `INTEGRITY_CRC` before PASS.
  Physical `1.0.0.12` testing then proved the exact P20/P21 payload and full
  G15 8 KiB programming, but repeated warm programming passed only 5/9.
  `decoder--260727-105439.csv` captured three PROGRAM failures at units
  `0x067B`, `0x0002` and `0x058F`; PA.9 before the resistor and TOOL0 both
  showed one transition delayed by about 8.7 us, exactly one bit period.
  Source correlation found two 64-byte PC UART report copies protected by
  PRIMASK. A GUI status poll could therefore suppress one priority-zero TIMER2
  overflow despite UART0 itself being priority three. Firmware `1.0.0.13`
  masks only `UART02_IRQn` in those critical sections. The build script rejects
  global interrupt masking in the PC UART adapter.
  The subsequent reset/connect/image-switch run
  `Rl78ProgrammerTool_20260727_111825_767_3048.log` contained seven P21 jobs
  followed by five P20 jobs with exact, non-overlapping UUIDs, proving that the
  GUI did not reuse a stale package. The analyzer captured all twelve entry
  sequences: P21 passed 7/7 and P20 passed 3/5. Byte-level comparison exposed
  an acknowledged P21 corruption at offset `0x0B3A` (`A9 -> 09`) and a rejected
  P20 corruption at offset `0x12B6` (`A8 -> 08`). Both lost TX bit 7, so a
  target ACK and provider-side CRC did not prove physical-wire identity.
  Firmware `1.0.0.14` polls the TIMER2 period flag for start/data/parity inside
  one bounded approximately 87 us interval, then releases PB.9, restores
  interrupts and arms PB.10 RX at the first stop bit. The Keil build requires
  this deterministic TX path. The GUI connection log now records reported
  product and firmware version.
  The first confirmed capture contained thirteen complete transactions. The
  additional `decoder--260727-131239.csv` capture added seven more: three P21
  and four P20, all with 2,048 acknowledged units, final verify ACK and an
  exact selected-image payload. This closes the requested Online sample at
  20/20. `decoder--260727-131515.csv` captured 22/22 autonomous PC.2 Offline
  P21 jobs with the same complete payload and verify evidence.
  The same PC log also exposed a stale-image UI hazard: after the displayed
  path changed from P20 to P21 without validation, `JOB_START` still used the
  P20 package UUID and the captured payload matched P20. The GUI now binds the
  validated image/OIP to the exact displayed path, disables path editing while
  busy, and rejects a validation completion if the path changed.
  The PC UART transport also drains each complete request before COM close so
  the no-response `Run Application` command reaches the resident bootloader.
- Nu-Link2-Me: M484SIDAE Release project, HSUSBD 64-byte HID, W25Q16 SPIM
  backend, KEY1 offline trigger, four-LED manager and split PA.9/PA.10 TOOL0
  adapter implemented. The audited wiring uses one external 47 Ω series resistor
  per branch, merges them only at the target-side TOOL0 node, and adds a 1 kΩ
  TOOL0 pull-up to the target's actual 3.3 V VDD. Corrected 2026-07-20 physical
  HIL completed 20/20 warm G15 8 KiB programming runs and 10/10 runs after
  bridge-board power cycling. Each logic-decoder transaction contained all
  2,048 four-byte writes, every `0x06` ACK and the final verify ACK; repeated
  payload hashes were stable. A later autonomous KEY1 capture completed 13/21
  attempts: 8/13 with PA.9/PA.10 analyzer probes and 5/8 after removing them.
  Observable failed first-half attempts contained 0.15-1.84 ms intervals where
  PA.9 was low while PA.10/TOOL0 remained high. A nominal 2 ms-guard follow-up
  completed only 3/16 attempts and was removed. A 2026-07-22 SRAM-cache rerun
  with a new direct Nu-Link2-Me-to-G15 ground entered 45/45 complete
  handshakes, but only 12/45 jobs sent all 2,048 units and the final verify ACK;
  the other 33 stopped at varying data units. PA.9/PA.10 were not probed in
  that capture. A later 116.78-second no-probe run completed 34/45; seven
  failures were exact-prefix truncations and four contained corruption. Its
  final PASS exposed that single-result retention erased earlier failure
  snapshots. The current build keeps the PA.10 low-drive echo check,
  preloads the target image into a validated 64 KiB SRAM cache, hard-cleans the
  TOOL0 peripheral state before every attempt and exposes 64 volatile offline
  failure records through `GET_OFFLINE_FAILURE_HISTORY`. The first
  history-enabled run completed 25/38 jobs; all 13 retained job counters matched
  the 13 LA truncations and reported program-stage direction collision. The
  superseded reset-only image then completed 9/28; all 19 failures were the same
  collision and every one-second reset appeared in LA. Firmware `1.0.1.4` added
  immediate terminal target park and bounded collision-only full-job retry as
  described below. After a complete bridge/target power cycle, that image then
  completed 64/64 normal KEY1 transactions with the exact P21 payload, all
  2,048 data ACKs and the final verify ACK; no collision retry was exercised.
  Firmware `1.0.1.5` changes the app-issued Nu-Link2-Me reset from CPU-only to
  full M484 chip reset. The formal target
  defines the compile-time target gate as `1` for physical HIL; it is not
  production-approved firmware.
- Build: all three board profiles now have resident bootloader and application
  Keil projects. The M032
  `demo_code/M032BSP_RL78_Programmer/{Library,SampleCode}` tree and the
  Nu-Link2-Me sample tree use relative paths; their projects emit boot/
  application AXF/HEX/BIN/map and factory images with the approved ADR-0011
  partitions and 0 warnings. MCU output remains in each Keil project's
  `obj/lst`; root `build` is limited to the PC EXE, portable INI and transient
  `obj` content. `clean_mfc.bat` leaves only the EXE and INI.
- Verification: the current native Debug and Release runs both pass 243/243;
  MFC Debug and Release build with zero warnings/errors. Fourteen
  portable firmware files pass armclang `-std=c90 -pedantic -Werror`; eleven
  M032 NuMicro adapters pass armclang `gnu90`. All three board boot/application Keil
  Release builds pass; the M032 and Nu-Link2-Me mirrors contain 72 canonical
  C/H files after synchronization. M484 adapters and the
  vendor-derived ISP_HID projects are covered by Keil builds, not a separate
  strict-`-std=c90` claim. See `SELF_CHECK.md`; target HIL is partial and the
  corrected ACK turnaround and online G15 programming have passed the board
  rerun. Autonomous Offline KEY1 is functional, but its 13/21, guarded 3/16 and
  direct-GND SRAM-cache 12/45, 34/45 and history-enabled 25/38 stability results
  are not acceptable; the recovery-enabled HIL rerun remains required.
  An isolated temporary-EXE smoke test also passed the missing-key, enable,
  relaunch/new-log and disable transitions for `SaveLogToFile=0/1`.
- Hardware target programming: Nu-Link2-Me HSUSB/W25Q16/RESET/TOOL0 online G15
  erase/program/internal-verify passed the 20-run warm and 10-run power-cycle
  matrix. Autonomous KEY1 completed 13/21 before the guard experiment and only
  3/16 with it, then 12/45, 34/45 and history-enabled 25/38 with SRAM caching/
  direct target ground. G16, M032 and recovery-enabled Offline KEY1/Reset stability
  testing remain required before release use.

## Implemented behavior

- The GUI main window opens maximized on initial launch. The standard Windows
  restore, minimize and maximize controls remain available.
- Program and Offline display the same target profile, firmware path, Browse,
  Load + Validate and image/OIP summary, so they cannot silently reference two
  different target files. Load + Validate automatically creates the OIP in
  memory; Save OIP is an optional export rather than a required input step.
- Offline shows storage-download byte progress and five workflow rows for
  validation, board-storage download, exact-package activation, arming and
  physical KEY1/Reset triggering. A changed source/profile clears downstream
  state; Activate and Arm/Test stay disabled unless the session-downloaded and
  bridge-reported active UUIDs match the current generated OIP. The physical
  step remains Ready when HID is disconnected rather than claiming completion.
- GUI communicates only through Programmer SDK.
- GUI exposes the fifth `FW Upload (USB HID)` tab. It keeps the bridge APP BIN
  separate from RL78 images and provides profile selection, fixed-size/CRC
  validation, `Return Boot loader`, ISP scan/selection with full HID paths,
  upload/cancel and explicit `Run Application` actions.
- Program and FW Upload show percentage, loaded/total KiB, programmed KiB and
  remaining KiB. Programming bytes come from the device job/bootloader counters;
  a bootloader scan with no validated bridge image explicitly directs the user
  to `Load + Validate` before `Upload Firmware` can be enabled.
- FW Upload uses five session-local workflow rows for image validation,
  bootloader return/scan, upload and application run. Completed rows are gray;
  working/failed rows remain prominent. Return or a new scan clears the stale
  bootloader path before downstream actions can be enabled.
- The `Log` tab's `Save log to file` checkbox defaults cleared on first use or
  when its key is missing. The user choice persists in portable
  `rl78_programmer.ini` as `[Programmer]` `SaveLogToFile=0/1`. A stored `1`
  restores the checkbox and automatically creates a new uniquely named UTF-8
  session log under `logs` beside the running EXE on each launch; an earlier
  log is never reopened for append. Disabling stops disk recording immediately
  without stopping the on-screen Log or History. `Clear Log` clears only the
  on-screen view. File create/write failures are visible, clear the checkbox
  for the current run and do not silently fall back to another path.
- Terminal programming diagnostics record mode, job UUID, result, stage name
  and numeric ID, error name and hexadecimal code, detail, attempt, elapsed
  time and final progress. The current-session History view exposes `Detail`
  as a separate column.
- SDK discovers Win32 HID devices and negotiates PLP 1.0 sessions.
- SDK closes with the programmer-issued session UUID. Firmware validates and
  echoes that UUID, clears session ownership on close/transport disconnect, and
  lets a physical offline button reclaim only a stale idle session; an active
  host programming job is not reclassified as autonomous KEY1 work.
- PLP uses fixed 64-byte reports with CRC, padding and flag validation.
- PLP `FW_ENTER_BOOTLOADER` (`0x0506`) requires an idle negotiated session and
  programmer-update capability. The application erases its reserved CRC page
  before ACK, resets after response completion (500 ms bounded fallback), and
  the SDK closes locally without a PLP close command.
- PLP `RESET_PROGRAMMER` (`0x0007`) requires an idle negotiated session and
  programmer-reset capability. It preserves application CRC, ACKs before the
  same bounded reset path, and the GUI closes locally then requests a fresh
  application scan/reconnect.
- Both resident bootloaders enumerate as development `0416:3F00`, reject writes
  outside the exact application range, validate vectors/CRC before launch and
  stay in bootloader after an invalid or interrupted update. USB updates never
  replace the bootloader; initial provisioning uses a debugger/factory image.
- Both boot projects are APROM targets: M032 uses `M031_AP_128.FLM`, and M484
  uses `M481_AP_512.FLM`. Initial Nu-Link ICP/Keil provisioning must preserve
  unrelated CONFIG bits while setting `CONFIG0.CBS[7:6]=10` on both boards and
  `MBS bit 5=1` on M484. The host rejects an invalid required setting before
  erase with `UPDATE_PACKAGE_INCOMPATIBLE`; handoff preflight leaves the current
  application valid when this rejection occurs.
- The SDK validates fixed application BINs before ISP access: M032 starts at
  `0x4000`, size `0xBC00`, CRC offset `0xBBFC`; M484 starts at `0x4000`, size
  `0x7C000`, CRC offset `0x7BFFC`. The full HID path binds one bootloader
  instance; the SDK then matches the selected profile against its reported
  application geometry and rejects a mismatch before erase. PDID is displayed,
  not hard-coded as an allowlist. Bridge BINs are not OIP/RL78 images.
- ISP packet verification preserves the expected bytes, performs `ReadData`
  after programming, and only then compares readback. Both application-to-boot
  and boot-to-application paths mask interrupts before changing VECMAP, check
  ISP failure and confirm VECMAP readback. Bootloader FMC read/program/erase
  polling is bounded and fails the command on timeout.
- Every non-`.bin` input is content-detected from its first non-whitespace
  marker: `S` selects Motorola S-record and `:` selects Intel HEX. Extensions
  are hints only, so a `.mot` may contain Intel HEX. The selected parser is
  strict and a validation failure is never retried through the other parser.
  Raw BIN remains explicit, maps byte zero to Code Flash `0x00000000`, and must
  fit the exactly selected G15 4/8 KiB or G16 16/32 KiB profile. All supported
  formats produce the same deterministic OIP contract. The repository P20/P21
  `.mot` samples are Intel HEX, contain 6,402 data bytes each, and validate only
  for the G15 8 KiB profile.
- Package transfer supports begin/chunk/finalize/activate commands.
- Package storage validates the streamed OIP header, SHA-256 and canonical CRC,
  and journals activation/mode across two metadata pages. The approved internal
  APROM profile has one 64 KiB slot; starting replacement invalidates the old
  active package before erasing it.
- HID exposes Online Only/Offline Armed policy. Offline Armed starts only after
  an M032 external Reset and a 2-second no-session grace period; power-on does
  not start it. A GUI session suppresses a pre-erase retry and can switch back
  online without deleting the image.
- During a Nu-Link2-Me autonomous job's pre-erase `TARGET_CONNECT` state, a new
  debounced KEY1 press safely releases the current link and immediately reruns
  RESET/TOOL0/`0x3A` within the same job instead of waiting for the 500 ms
  deadline. The request is rejected after erase/program begins.
- OIP address classification comes from a supplied `TargetProfile`; Protocol B
  currently installs G15 4/8 KiB and G16 16/32 KiB exact profiles.
- Online and offline programming converge on the same firmware job state.
- Cancellation is cooperative and terminal PASS is explicit and queryable.
- Protocol B implements mode entry, size-code validation, whole-code erase,
  four-byte streaming, target internal verify and reset. Offline handshake
  failures release pins and retry every 500 ms; no destructive outcome is
  automatically retried or fabricated as PASS.
- UART1 PA.3/PA.2, reset PA.5 and VDD enable PA.4 use the Arduino digital-header
  side while avoiding EVB VCOM PB.12/PB.13; PB.3/D1 is NC and target-VDD sense
  is disabled. The sole PB.14 LED is the RL78 upgrade indicator.
- PB.14 active-low patterns cover ready, image transfer, programming/recovery,
  PASS and FAIL/CANCELLED states.
- Nu-Link2-Me reuses the same PLP/OIP/core with a distinct GET_DEVICE_INFO
  identity. Its active-low ICE/ISP/Red/Green patterns follow manual Table 2-4,
  and W25Q16 provides two 4 KiB journal sectors plus one `0x1FE000` package slot.
- Nu-Link2-Me uses TIMER2-driven PA.9/`SWDH_DAT`/ICEJ4-3 open-drain GPIO TX and
  PA.10/`SWDH_CLK`/ICEJ4-5 GPIO RX as separate external 47 Ω branches that merge
  only at the target-side RL78 TOOL0 node; TOOL0 has a 1 kΩ pull-up to the
  actual 3.3 V target VDD. PA.9 releases at the first stop bit; PA.10 uses
  falling-edge start detection, mid-bit 8-O-1 sampling and an 8-byte RX FIFO.
  A target falling edge during the released stop interval preempts the remaining
  TX timer bookkeeping so the Protocol B `tDRT` response is not dropped. No
  fixed millisecond guard is inserted after a four-byte ACK.
  While PA.9 drives a low data bit, PA.10 must observe low; otherwise the adapter
  reports `TARGET_LINK_DIRECTION_COLLISION (0x20907003)` and releases PA.9.
  Before every entry attempt, firmware releases PA.9, disables PA.10 start
  detection, module-resets TIMER2, clears GPIO/TIMER/NVIC pending state and
  clears the FIFO/error latch. It does not reset M484SIDAE for this cleanup.
- Nu-Link2-Me uses its own 3.3 V rail for the target side. PB.12/ETMD3_SS is NC,
  target-VDD sensing is disabled, and PB.13/ETMD2_CK is reserved. The current
  same-domain 3.3 V wiring needs no level shifter. External target supplies and
  5 V operation remain prohibited until a separate level interface is approved.
- Nu-Link2-Me online programming downloads and validates an OIP candidate in
  W25Q16, then expands the exact target code into a UUID/profile/size/CRC-bound
  64 KiB SRAM cache before target RESET. Offline use downloads, activates and
  arms the package; Offline Armed boot preloads the active package when valid.
  After HID disconnect/close with USB power retained, KEY1 starts the cached
  image. Package mutation invalidates the cache; W25Q16 remains authoritative.
- The last offline terminal result and newest 64 non-PASS records are retained
  in bridge RAM. Each failure records job counter/capture uptime, Protocol B
  step, unit address/byte index, TX byte/bit, TIMER2 state/count, PA.9/PA.10
  flags and destructive state before cleanup. Later PASS results do not erase
  the ring. The GUI emits `OFFLINE_FAILURE_HISTORY_SUMMARY` and one
  `OFFLINE_FAILURE_HISTORY` line per retained record. Reset/power-cycle clears
  this evidence; overflow overwrites the oldest record and reports dropped count.
- The first history-enabled HIL run contained 38 jobs: 25 full transactions and
  13 program-stage `TARGET_LINK_DIRECTION_COLLISION` failures. History job
  counters 8, 19, 21-24, 27-29, 31 and 36-38 matched the 13 LA truncations
  exactly; every snapshot showed PA.9 Low and PA.10 High.
- Nu-Link2-Me `FW 1.0.1.5` retains the one-second pre-erase connect recovery. A slow
  Green indication triggers it only when the job remains in `TARGET_CONNECT`;
  the same pre-erase job then immediately retries. Any terminal offline failure
  immediately releases TOOL0, cleans TIMER2/IRQ/FIFO state and holds G15 RESET.
  Only `TARGET_LINK_DIRECTION_COLLISION` starts a new full job after one second,
  from handshake/erase/address zero; two automatic retries are allowed per
  physical KEY1 (three total attempts). Exhaustion, cancellation, other errors
  and unknown outcome stay parked for the next KEY1. The M484 is not reset by
  this per-job recovery and normal Green programming activity is excluded.
- The `FW 1.0.1.5` application-issued `RESET_PROGRAMMER` and
  `FW_ENTER_BOOTLOADER` transitions use M484 full-chip reset after ACK completion
  or fallback. This clears internal GPIO/TIMER/SPIM/HSUSBD state before APROM0
  bootloader validation. The valid-application VECMAP jump remains CPU reset;
  W25Q16 and the RL78 target are not power-cycled. Physical reset/re-enumeration
  and subsequent KEY1 confirmation remain pending.
- Nu-Link2-Me enables `CoreDebug.DEMCR.TRCENA` before DWT `CYCCNT`, so the
  microsecond Protocol B delay source advances on M484SIDAE instead of hanging
  in the first bounded delay.
- Final M032 Keil Release: resident bootloader 8.13 KiB (7.87 KiB free) and
  application 19.02 KiB (27.48 KiB linkable free); fixed APP BIN `0xBC00`,
  CRC32 `0xAFFA6D2E` at `0x0000FBFC`, factory BIN size `0xFC00` ending before
  metadata at `0x0000FC00`;
  0 warnings/errors.
- Final M032 TEST BOARD Keil Release: resident UART bootloader 5.42 KiB
  (10.58 KiB free) and application 20.49 KiB (26.01 KiB linkable free);
  firmware `1.0.0.15`, fixed APP BIN `0xBC00`, CRC32 `0x0C02F625` at
  `0x0000FBFC`, factory image
  ending before metadata at `0x0000FC00`; application SHA-256
  `2F9B099E5A6C0612E8908A15A723DF819AD233B5CCF106788EF18DCB7350451D`;
  0 warnings/errors.
- Final M484SIDAE Keil Release: resident bootloader 4.53 KiB (11.47 KiB free)
  and application 24.08 KiB (467.92 KiB linkable free); fixed APP BIN
  `0x7C000`, CRC32 `0xAC381304` at `0x0007FFFC`, factory BIN `0x80000`, 0
  warnings/errors. Total RW/ZI is 73,528 bytes including the 2,048-byte stack;
  non-stack data/BSS is 71,480 bytes with more than 87 KiB remaining before the
  stack base. Bounded full-job recovery physical stability HIL remains pending.
- M032 and Nu-Link2-Me both skip target-VDD sensing per ADR-0009/ADR-0010. Both
  retain the 10 us TOOL0 setup and 1 ms hold before 115200 bps, 8-bit
  odd-parity Protocol B communication.

## Open hardware/product decisions

| ID | Decision needed | Owner | Current behavior |
| --- | --- | --- | --- |
| HW-001 | TOOL0 target-VDD level-interface schematic and turnaround waveform | Hardware | Software pin/mode/driver complete; HIL pending |
| HW-002 | RESET isolation, external target-voltage verification and optional PA.4 load-switch schematic | Hardware | PB.3 sense disabled; GPIO safe state implemented; HIL pending |
| HW-003 | Offline trigger and status LED mapping | Hardware | Existing M032 Reset is trigger; PB.14 patterns implemented |
| ST-001 | APROM power-cut/endurance evidence and continued boot/application partition margin | Firmware/Hardware | M032 boot 8.13 KiB, app 19.02 KiB; M032 TEST BOARD UART boot 5.42 KiB, app 20.49 KiB; one 64 KiB slot + metadata region approved |
| FWU-001 | Physical application/bootloader enumeration, selected-profile/reported-application-geometry binding, update interruption recovery and valid-app launch on both boards | Firmware/Hardware | M032 TEST BOARD UART ISP update/application reconnect passed; interruption recovery and other board HIL remain open |
| USB-001 | Production application/bootloader VID/PID and USB strings | Product | Development application defaults remain configurable; resident ISP currently uses `0416:3F00` |
| SEC-001 | Signing keys, provisioning and rollback policy | Security/Product | Production release blocked |
| DEV-001 | Exact RL78 orderable parts and option/security policy | Firmware/Product | G15 4/8 KiB and G16 16/32 KiB size profiles implemented |
| REN-001 | Captured Protocol B HIL verification | Firmware | Online G15 8 KiB passed 30/30; KEY1 completed 13/21 before, 3/16 with the removed guard, then 12/45, 34/45 and history-enabled 25/38 with SRAM cache/direct GND; recovery-enabled rerun pending |
| NL2-001 | Obtain schematic sheets 2/3 or record equivalent target-interface tracing/measurements | Hardware | Fixed 3.3 V HIL policy accepted; physical evidence pending |
| NL2-002 | Verify M484SIDAE marking, board 3.3 V target rail, PB.12 NC state, SWDH_DAT/SWDH_CLK passive-Y TOOL0 fixture, TICEDAT/TICECLK isolation and PF.5 reset | Hardware/Firmware | 3.3 V/RESET/TOOL0 programming passed; latest no-probe direct-GND run completed 25/38 and correlated every failure; recovery-enabled rerun pending |
| NL2-003 | Verify HSUSB, W25Q16 JEDEC/read/write/erase/power-cut, KEY1 and Table 2-4 LEDs | Firmware/Hardware | HSUSB/W25Q16/KEY1 path exercised; KEY1 stability, power-cut and LED acceptance remain pending |
| MTB-001 | Verify the M032 TEST BOARD external-VCOM UART, PB.9/PB.10 GPIO/TIMER2 software TX/RX TOOL0, PB.8 RESET, PC.2 offline/forced-ISP behavior and APROM slot under power interruption | Firmware/Hardware | VCOM/UART ISP passed; deterministic `1.0.0.14` G15 8 KiB HIL passed 20/20 Online and 22/22 PC.2 Offline with exact payloads/final verify ACKs; `1.0.0.15` LED mapping is built; G16, LED observation and power-interruption HIL remain open |
| PLP-STREAM-001 | Host-fed online programming without complete package staging | Architecture/SDK/Firmware | Deferred negotiated PLP extension; V1 continues full package staging |

## Next development slice

1. Upload the current
   `demo_code/M032BSP_RL78_Programmer/SampleCode/RL78_Programmer/KEIL/obj/M032TestBoardBridge.bin`
   over UART ISP, run the application, and connect with the newly built GUI.
   Confirm that the file log contains `firmware=1.0.0.15`, then observe both a
   GUI-started Online job and a PC.2 Offline job: programming must blink Red at
   125/125 ms, PASS must latch Green and an induced terminal failure must latch
   Red. The TOOL0 programming path is unchanged from the 20/20 Online and
   22/22 Offline `1.0.0.14` evidence.
2. Hold PC.2 during reset to force UART ISP, upload
   `M032TestBoardBridge.bin`, verify `MTB1` geometry/range rejection, valid
   application launch and interrupted-update recovery.
3. Flash `demo_code/M032BSP_RL78_Programmer/SampleCode/RL78_Programmer/KEIL/obj/M032Rl78Bridge_factory.hex` on M032SE3AE,
   verify PID `0x5020`, use `Return Boot loader` to verify `0416:3F00`, upload
   the fixed application BIN and verify return to PID `0x5020`.
4. Verify TOOL0/RESET/external VDD measurement and PB.3 NC behavior; complete
   ADR-0004/ADR-0010 HIL.
5. Verify TOOL0 direction, 115200 8-O-2 transmit/8-O-1 receive behavior and
   command timing on a logic analyzer.
6. Run the four-profile G15/G16 size-mismatch/erase/program/internal-verify HIL
   matrix and approve option/security policy.
7. Run Reset-triggered offline power interruption and 500 ms reconnect tests.
8. Flash the M484SIDAE
   `demo_code/M480BSP_Nulink2Me_RL78/SampleCode/Template/Keil/obj/NuLink2Me_boot_app.hex`, verify application/
   ISP HSUSB re-enumeration and interrupted app-update recovery, then verify
   W25Q16 JEDEC/read/write/erase, PB.12 NC state, KEY1 and the four LEDs without
   connecting an RL78 target.
9. Upload the current
   `demo_code/M480BSP_Nulink2Me_RL78/SampleCode/Template/Keil/obj/NuLink2MeRl78Bridge.bin`,
   confirm the connected device reports firmware `1.0.1.5`, press GUI
   `RESET MCU`, then scan/reconnect without cycling power. Disconnect the GUI
   while retaining USB power and press KEY1 to verify the clean-reset behavior.
   Connect only 3.3 V VDD/TOOL0/RESET/GND to RL78/G15 and run `HIL-NL2-011`:
   at least 20 consecutive KEY1-triggered 8 KiB offline jobs with 2,048 data ACKs
   and final verify ACK each. Confirm target traffic begins only after SRAM
   preload. Repeated KEY1 jobs may run for the complete two-minute LA window.
   Afterwards, do not reset or remove USB power: reconnect the GUI and preserve
   `OFFLINE_FAILURE_HISTORY_SUMMARY` plus every `OFFLINE_FAILURE_HISTORY` line
   with the LA capture. Confirm `dropped=0` before accepting the correlation.

## Build entry points

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_mfc.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File .\scripts\build_mfc.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\scripts\build_native_tests.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\scripts\build_firmware_core.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\sync_m032_sample.ps1 -Check
powershell -ExecutionPolicy Bypass -File .\scripts\build_m032_keil.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build_m032_test_board_keil.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\sync_nulink2me_sample.ps1 -Check
powershell -ExecutionPolicy Bypass -File .\scripts\build_nulink2me_keil.ps1
.\scripts\clean_mfc.bat
```
