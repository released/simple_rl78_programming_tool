# ADR-0012: M032 Test Board UART Programmer Profile

- Status: Accepted / implemented / physical HIL pending
- Date: 2026-07-23
- Extends: ADR-0002, ADR-0003, ADR-0007, ADR-0010 and ADR-0011

## Context

The M032 TEST BOARD fixture contains an M032SE3AE and exposes no USB device
connector. It does expose a 3.3 V TTL UART, one active-low push button, two
active-low status LEDs and three resistor-pulled fixture signals that can be
adapted to the RL78 TOOL0/reset programming interface.

The product must add this board without duplicating the Programming Engine,
OIP/package flow or target profile implementation. Online programming, package
download/activation, autonomous offline programming, failure history and bridge
application update must remain available over the board's UART connection.

The controlled sources are:

- An authorized private fixture schematic (not redistributed)
- An authorized private fixture photograph (not redistributed)

Both sources show that connector names A11/A10/A9 are shifted from an earlier
proposed MCU-port interpretation. The physical nets are A11/PB.10,
A10/PB.9 and A9/PB.8. PB.11 is the separate `TH` net.

## Decision

### Board identity and host selection

- The GUI board-profile option is exactly `M032 TEST BOARD`.
- The application reports a distinct product identity and board-profile UUID.
  M032 device ID and APROM geometry alone do not distinguish this fixture from
  the NuMaker-M032SE profile.
- The PC SDK selects a transport through an interface. USB HID and UART are
  transport implementations; the GUI and Programming Engine do not parse or
  access either transport directly.
- `Program`, `Offline`, `History` and `Log` use the same PLP/application logic
  for USB HID and UART boards.
- The existing bridge-firmware tab is transport-contextual: it is titled
  `FW Upload (USB HID)` for USB boards and `FW Upload (UART ISP)` for
  `M032 TEST BOARD`. Its device discovery, instructions and updater backend
  switch with the selected board; incompatible USB VID/PID controls are
  disabled for the UART profile.

### PC-to-bridge UART

- PC/VCOM TX connects to M032 `MCU_RX`/PF.2/UART0_RXD.
- PC/VCOM RX connects to M032 `MCU_TX`/PF.3/UART0_TXD.
- The electrical interface is 3.3 V TTL and common ground. RS-232 voltage and
  5 V UART logic are prohibited.
- Application and resident-bootloader UART start at 115200 baud, 8 data bits,
  no parity and 1 stop bit.
- Application PLP retains the existing exact 64-byte report and CRC16
  contract. The UART receiver finds `PLP_MAGIC`, collects 64 bytes and accepts
  only a complete CRC-valid report. Invalid or truncated stream content is
  discarded with bounded resynchronization.
- The resident UART bootloader retains the bounded Nuvoton 64-byte ISP packet
  flow, but hard-rejects any APROM write outside the fixed application image.

### RL78 programming pins

| Fixture net | M032 pin | Role | Required state |
| --- | --- | --- | --- |
| A11 | PB.10, package pin 61 | TOOL0 software receive | GPIO falling-edge input plus TIMER2 sampling |
| A10 | PB.9, package pin 62 | TOOL0 transmit drive | TIMER2 GPIO open-drain; Low or high impedance only |
| A9 | PB.8, package pin 63 | RL78 reset | Active-low GPIO open-drain |
| SW | PC.2, package pin 38 | Offline/boot-recovery button | Active-low debounced input |
| LED-R | PA.11, package pin 8 | Program/fail/ISP indication | Active-low output |
| LED-G | PA.10, package pin 9 | Ready/pass indication | Active-low output |

A11 and A10 each pass through an external 47-ohm series resistor before they
join the single RL78 TOOL0 node. They are never shorted together on the MCU side
of those resistors. A9 may use a 100-ohm protection resistor before RL78 RESET.
The fitted 30-kohm pull-ups on A11/A10/A9 remain installed. A target-side
1-kohm TOOL0 pull-up to the actual 3.3 V target rail remains recommended.

PB.9 and PB.10 use the Nu-Link2-Me GPIO/TIMER2 software-UART model. PB.9 is
open-drain 115200 8-O-2 transmit. PB.10 is GPIO falling-edge receive with
TIMER2 sampling for 115200 8-O-1. HIRC48 uses 417 ticks per bit. On M032, the
validated PB.10 falling edge starts one periodic timer and the first event
samples data bit 0. It deliberately omits the M484 half-bit confirmation
interrupt and second timer restart: physical firmware `1.0.0.10` evidence
decoded valid `0x3A -> 0x36` on all three attempts, while the bridge reported
framing because the two Cortex-M0 scheduling latencies placed its first sample
too late. UART0 remains dedicated to the PC/VCOM link.

PB.10 edge detection is disabled during entry Low and transmit data. It is
enabled when PB.9 releases for the first stop bit. A valid falling edge during
the released stop interval transfers TIMER2 ownership from TX bookkeeping to
RX start-bit confirmation so the target turnaround is not lost. Consequently,
the transmit waveform and entry Low never enter an RX FIFO and no echo-removal
queue or UART4 reset is required. PB.9-Low/PB.10-High at a transmitted Low-bit
boundary remains the direction-collision test.

The earlier UART4 receiver path reached valid physical `0x3A -> 0x36`
exchanges but repeatedly returned an entry-derived `0x00` before the target
response. FIFO purging at release and immediately before TX did not eliminate
the receiver-internal timing dependency. Firmware `1.0.0.10` therefore
supersedes that mixed hardware/software path with the same explicit software
RX ownership model already used by Nu-Link2-Me. Firmware `1.0.0.11` retains
that ownership model and changes only the M032 receive-start schedule to the
single periodic timer described above.

The physical `1.0.0.11` rerun then completed entry, erase, all 2,048 G15 8 KiB
program units and final target verify, but the decoder showed all 8,192 payload
bytes were `0xFF`. This was not a TOOL0 timing failure. `prepare_attempt()`
restored SYS write protection after the package header was opened; the later
storage provider used `FMC_Read()`, whose `ISPTRG` write is protected, and
therefore returned stale `ISPDAT=0xFFFFFFFF`.

Firmware `1.0.0.12` makes memory-mapped APROM access the required read path for
the internal Package slot. Protected FMC commands remain required for page
erase and word program only. As a second fail-closed boundary, Protocol B
calculates the exact-profile dense-image CRC before target entry and separately
updates a CRC from every four-byte programming unit loaded after attempt
preparation. A mismatch terminates with `INTEGRITY_CRC` before target verify can
be accepted. This protects against a storage-provider state change; it does not
replace the target's Protocol B internal verify.

Every attempt begins by releasing PB.9, disabling PB.10 edge detection,
resetting TIMER2 and software RX state, and holding/releasing target reset in
the approved sequence. The TOOL0 GPIO and TIMER2 interrupts run at the highest
NVIC priority. The PC/VCOM UART0 interrupt runs at the lowest priority so a
64-byte PLP receive or transmit service cannot delay TOOL0 bit generation.
UART0 report-buffer critical sections mask only `UART02_IRQn`; they do not use
PRIMASK or a global interrupt-disable instruction. This rule is stronger than
the IRQ-priority assignment because PRIMASK would also block priority-zero
TIMER2 and can discard one periodic overflow.
Software parity, stop-bit or receive-FIFO validation failure is reported as
`TARGET_LINK_UART_FRAMING`, not as an official target status. Online mode
performs at most three complete handshake attempts, separated by the 500 ms
safe retry interval, and only before erase permission; destructive operations
are never blindly restarted.

TIMER1, clocked directly from HIRC at 48 MHz, is reserved for reset/TOOL0
microsecond entry delays. It runs in polling one-shot mode without an IRQ.
SysTick remains the independent 1 ms source for Protocol B deadlines and
retry scheduling. The generic NuMicro `CLK_SysTickDelay()` is prohibited in
this application because it disables SysTick when its delay completes; using
it made the first entry delay stop all `TARGET_CONNECT` timeout and retry
advancement. Nu-Link2-Me provides the same clock separation with DWT rather
than TIMER1.

### Offline operation and status

- A short debounced PC.2 press in the application starts one offline job only
  when a validated active package and destructive policy permit it.
- Holding PC.2 during reset or power-on forces the resident UART ISP
  bootloader to remain available even when the application CRC is valid.
- Online and offline programming both flash Red at 125 ms on / 125 ms off.
  Terminal PASS latches solid Green; FAIL, CANCELLED, interrupted or outcome
  unknown latches solid Red. Application idle remains a slow Green blink.
- Resident UART ISP waits with solid Red and pulses Green for accepted packets.
  Its solid Red must be interpreted in the bootloader/update context rather
  than as an Application programming result.
- Application and target programming recovery reuse the existing bounded
  Protocol B state machine. Per-job retry does not reset the whole M032.

### APROM layout

The existing M032 partition remains authoritative:

| Region | Start | End inclusive | Size / rule |
| --- | ---: | ---: | --- |
| Resident UART ISP bootloader | `0x00000000` | `0x00003FFF` | 16 KiB; debugger-only replacement |
| Linkable application content | `0x00004000` | `0x0000F9FF` | 46.5 KiB ceiling |
| Application CRC erase page | `0x0000FA00` | `0x0000FBFF` | 512 bytes |
| Fixed application image | `0x00004000` | `0x0000FBFF` | `0xBC00` bytes / 47 KiB |
| Metadata journals | `0x0000FC00` | `0x0000FFFF` | two 512-byte pages |
| RL78 Package slot A | `0x00010000` | `0x0001FFFF` | 64 KiB, single slot |

The M032 has 16 KiB SRAM and cannot copy the M484 64 KiB whole-image cache.
The M032 internal APROM storage provider remains authoritative and uses
memory-mapped reads while target programming is active. Package replacement
and power-loss recovery retain the existing journal behavior.

The internal APROM backend clears stale FMC failure state and reasserts
`ISPEN/APUEN` before every destructive command. Page erase and word program
execute inside short interrupt-masked command windows and are followed by
read-back verification. Non-destructive Package-slot reads do not use
`FMC_Read()` or `ISPTRG`, so they remain valid after SYS registers are locked.
The application reserves a 4 KiB stack; long-lived
board, transport, target and storage objects reside in static RAM.

### 2026-07-27 repeated-programming timing decision

Firmware `1.0.0.12` physically programmed the exact P20/P21 payload and
completed full 8 KiB transactions, closing the earlier all-`FF` defect. A
subsequent nine-run warm-programming log passed five runs and failed four in
PROGRAM. Three captured failures occurred at programming units `0x067B`,
`0x0002` and `0x058F`. In each failure, PA.9 before the 47-ohm resistor and
TOOL0 both showed one transmit transition delayed by about 8.7 us, exactly one
115200-bit interval; the G15 then returned status `0x15`. The error therefore
originated in M032 transmit scheduling, not the resistor merge or target
turnaround.

The delayed bytes followed a long PC status-poll interval. Source correlation
found that UART0 PLP `Send` and receive-report handoff copied a 64-byte report
while PRIMASK globally disabled interrupts. If this overlapped a TOOL0 byte,
one TIMER2 periodic interrupt was coalesced and could not be recovered.
Firmware `1.0.0.13` replaces both global critical sections with
`UART02_IRQn`-only masking. The Keil build rejects regression to
`__disable_irq()`/PRIMASK in this adapter. Physical repeated online/offline HIL
intended to exercise `1.0.0.13` then showed that this isolation was necessary
but not sufficient.

The next 12-run capture used the explicit
RESET MCU -> Connect -> Load + Validate -> Program Online sequence while
switching from P21 to P20. The PC log proved exact UUID separation: seven P21
jobs used `a64218ad-3d84-556a-899d-d27a28061f24`, followed by five P20 jobs
using `1e2084be-7c98-5766-b428-dbb678a7454d`. The analyzer contained twelve
matching entry sequences. P21 passed 7/7; P20 passed 3/5, with target NAKs at
program units 1197 and 2. This rules out stale GUI image selection.
The legacy GUI log did not include the programmer-reported firmware version,
so the file alone cannot prove that the intended `1.0.0.13` image was actually
running.

Byte comparison found a stronger safety issue. One P21 transaction reported
PASS but transmitted `0x09` instead of expected `0xA9` at target offset
`0x0B3A`; its odd parity remained valid and the target acknowledged the byte.
The failed P20 unit transmitted `0x08` instead of expected `0xA8` at offset
`0x12B6`. Both failures omitted bit 7, proving that target ACK plus a
provider-side CRC does not by itself prove physical-wire byte identity.

Firmware `1.0.0.14` therefore makes outgoing start/data/parity timing
independent of interrupt delivery. It polls the 417-tick TIMER2 hardware flag
and drives each of those ten bits once inside an approximately 87 us bounded
PRIMASK interval. PB.9 is released, PB.10 falling-edge receive is armed and
interrupts are restored at the first stop bit. The existing priority-zero
PB.10/TIMER2 receive path handles target turnaround. UART0 can place at most
two newly completed host bytes in its hardware FIFO during the bounded
interval. The Keil
build requires the polling path, and the GUI connection log records the
reported product and firmware version for HIL provenance.

The resulting `1.0.0.14` binary passed 20/20 captured G15 8 KiB Online jobs.
The final seven-run capture alternated three P21 and four P20 images; all seven
contained 2,048 acknowledged four-byte units, a final verify ACK and an exact
8,192-byte selected-image payload. The PC.2 autonomous capture passed 22/22
Offline P21 jobs with the same complete payload and final verify evidence.
Firmware `1.0.0.15` leaves this TOOL0 transport unchanged and changes only the
Application LED policy to the Red-programming/Red-failure/Green-PASS mapping
above.

### UART application update

- Initial provisioning writes the resident UART ISP bootloader and application
  with a debugger and sets APROM-with-IAP configuration as required by
  ADR-0011.
- PLP `FW_ENTER_BOOTLOADER` invalidates the application CRC page, completes the
  UART response through the final UART0 `TXEND`, releases the PC COM handle,
  then resets through APROM address zero.
- The bootloader stays in UART ISP when the application is invalid or PC.2 is
  held during reset.
- `CMD_RUN_APROM` clears stale FMC failure state, verifies the fixed
  application image CRC and performs a full-chip reset. The clean resident
  boot path verifies the CRC again, maps vectors to `0x00004000` and performs
  the application CPU reset. This avoids carrying UART/FMC/SysTick state from
  the update session into the application.
- The PC serial transport drains the external-VCOM transmit queue after each
  complete 64-byte request. This is required for no-response commands such as
  `CMD_RUN_APROM`, because closing the COM handle immediately after an
  overlapped write may otherwise cancel bytes that have not left the adapter.
- The GUI accepts only the `M032 TEST BOARD` fixed `0xBC00` application BIN,
  validates its CRC32, confirms the bootloader board identity and geometry
  before erase, programs only the application range and verifies readback.
- `Scan Bootloader` does not equate OS COM enumeration with ISP presence. It
  performs a bounded NuvoISP probe and lists only a candidate that reports
  `MTB1` and the exact application geometry.
- The bootloader, metadata and Package slot A are never replaced by GUI
  application upload. An interrupted upload remains recoverable through the
  same COM port.
- Software RX is disabled during RESET/TOOL0 entry and outgoing data. It is
  armed at the first transmit stop bit and therefore cannot convert the entry
  Low or transmit echo into received bytes.
- The external decoder may still display the RESET/TOOL0 entry hold as an
  invalid `0x00`; that observation is outside the disabled software-RX window.

## Consequences

The shared Program and Offline workflows remain transport-independent, but the
current PC SDK and M032 application USB-specific port names must be generalized.
The UART bootloader needs a board-identity query beyond the common M032 device
ID. The application and bootloader own UART0 at different times and the GUI
must close/purge the old protocol state before probing the new one.

M032 HIRC has no USB SOF trim on this board. TIMER1 entry-delay timing,
TIMER2 TOOL0 timing, UART0 baud
error and temperature/voltage stability are physical release gates. A fixed or
fractional 416/417-tick schedule at 48 MHz must be confirmed with a logic
analyzer before the profile is production-approved.

## Required Verification

- A11/A10/A9 continuity to PB.10/PB.9/PB.8 and all fitted pull-up values.
- 3.3 V VCOM levels, common ground and UART0 115200 8-N-1 PLP traffic.
- UART ISP forced-button entry, invalid-CRC stay-in-boot, fixed-range update,
  interruption recovery, readback and valid-app launch without a board power
  cycle.
- TIMER2/PB.9 software transmit, GPIO-edge/TIMER2/PB.10 software receive,
  8-O-2/8-O-1 framing, first-stop-bit turnaround, collision detection and
  retry timing.
- G15 4/8 KiB and G16 16/32 KiB online programming.
- Package download, activate/arm, power-cycle recovery and repeated PC.2
  autonomous offline programming from internal APROM.
- Red/Green LED behavior and safe target park on every terminal result.
