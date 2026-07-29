# ADR-0008: Nu-Link2-Me RL78 Programmer Board Profile

- Status: Accepted as 3.3 V HIL candidate / partial physical verification,
  history correlation passes and recovery-enabled repeat HIL is pending
- Date: 2026-07-13
- Updated: 2026-07-22 for direct-GND HIL, bounded failure history and one-second link recovery
- Extends: ADR-0002 and ADR-0007 with a second programmer-board profile
- Superseded in part by: ADR-0009 replaces the original PB.12 target-VDD
  interlock; the current no-sense policy is reflected below

## Context

The Nu-Link2-Me hardware is to be reused as a dedicated RL78 programmer while
preserving the platform, PLP, SDK, image-provider and target-driver boundaries.
The board provides an M484SIDAE MCU, HS USB, a 16-Mbit serial flash, an offline
button, four status LEDs and the existing target connector.

The supplied schematic PDF contains only sheet 1 even though its title block
says `Sheet 1 of 3`. It therefore proves MCU pin allocation and the components
on that sheet, but not the target-side circuits behind the off-page
`TICEDAT`, `TICECLK`, `TICERST`, `MCU_TX` and `MCU_RX` nets. The schematic MCU
symbol is written as `<M48SSIDAE>`; the corrected product selection is
M484SIDAE because this board uses the HS-USB device path. The fitted marking
remains a physical HIL acceptance check.

## Decision

Nu-Link2-Me is a separate board-profile/HAL implementation. It does not change
the reusable RL78 Protocol B driver or let upper layers access M484 registers
directly.

### M484SIDAE board resources

The selected M484SIDAE provides 512 KiB APROM and 160 KiB SRAM. The formal
Keil target and linker map must use this exact device and memory geometry.

The following mapping is taken from sheet 1 of the supplied schematic. Package
pin numbers are included so the implementation can be checked against the
actual assembled board.

| Resource | Schematic net | M484SIDAE GPIO / function | Package pin | V1 use |
| --- | --- | --- | --- | --- |
| RL78 transmit data | `SWDH_DAT` | PA.9 / GPIO open-drain | 10 | TIMER2-driven TOOL0 software transmit; released/high impedance for logical High and at safe boot |
| RL78 receive data | `SWDH_CLK` | PA.10 / GPIO input | 9 | Falling-edge start detection and TIMER2 mid-bit software receive |
| RL78 reset | `SWDH_RST#` | PF.5 | 14 | Active-low target reset, released at safe boot |
| Offline button | `KEY1` | PC.7 | 18 | Debounced active-low request; 10-kohm pull-up to `ICEVDD`, switch to GND |
| ICE LED | `ICELED` | PF.4 | 15 | Active-low status output |
| ICP LED | `ISPLED` | PC.6 | 19 | Active-low status output; this is the manual's `ICP` LED |
| Red LED | `RED` | PB.9 | 62 | Active-low status output |
| Green LED | `GREEN` | PB.8 | 63 | Active-low status output |
| SPIM MOSI | `SPIM_MOSI0` | PC.0 / SPIM_MOSI | 40 | W25Q16 data output through level translator |
| SPIM MISO | `SPIM_MISO0` | PC.1 / SPIM_MISO | 39 | W25Q16 data input to M484 |
| SPIM clock | `SPIM_CLK` | PC.2 / SPIM_CLK | 38 | W25Q16 clock through level translator |
| SPIM chip select | `SPIM_CS` | PC.3 / SPIM_SS | 37 | W25Q16 chip select through level translator |
| HXT input | `HXT_I` | PF.3 | 16 | 12-MHz crystal input |
| HXT output | `HXT_O` | PF.2 | 17 | 12-MHz crystal output |
| Reserved, no VDD sense | `ETMD3_SS` | PB.12 | 56 | NC in the accepted profile; firmware does not configure EADC or enforce a voltage interlock |
| Reserved sense/control | `ETMD2_CK` | PB.13 | 55 | Reserved; not used by the V1 target interface |

The ICEJ4 target connector assigns VCC, data, clock, reset and ground to pins
1, 3, 5, 7 and 9 respectively. Its even pins carry BUSY, START, SWO,
`ICE_RX_S` and `ICE_TX_S`. The connector is marked `(NC)` in the schematic, so
connector population and continuity must be checked on the actual board.

M484 UART does not expose the M032 single-wire selection used by the first
bridge. The accepted implementation therefore uses a GPIO software UART:
PA.9 is an open-drain-only transmitter and PA.10 is an input-only receiver.
TIMER2 runs from the 12 MHz HXT at 104 ticks per bit (approximately 115384.6
bps, +0.16% from 115200). Programmer transmit is 8-O-2; PA.9 is released and
PA.10 falling-edge detection is enabled at the first stop bit. Receive start
is confirmed at half a bit and the 8-O-1 response is sampled at subsequent bit
centers into an eight-byte software FIFO. GPA and TIMER2 interrupts have higher
priority than HSUSB, and no Protocol B policy executes in either ISR.
If the target response starts while the released PA.9 transmitter is still
finishing its second-stop-bit timer interval, the PA.10 falling-edge ISR takes
ownership of TIMER2 and begins receive sampling. This prevents the target start
edge from being discarded by stale transmit-busy state while preserving the
physical 8-O-2 high level for ordinary back-to-back programmer bytes.
After each four-byte program ACK, the driver may start the next unit without a
fixed millisecond guard. While PA.9 transmits a Low bit, PA.10 verifies at the bit boundary that
the merged TOOL0 node also became Low. A mismatch stops the timer, releases
PA.9 and reports `TARGET_LINK_DIRECTION_COLLISION (0x20907003)`; a partially
observed byte is never retried in place.

Schematic page 1 confirms that `SWDH_DAT` and `SWDH_CLK` run directly to ICEJ4
pins 3 and 5 without onboard series resistors. The external fixture joins
these branches to RL78 TOOL0 after separate external 47-ohm resistors. PA.9
never drives push-pull High; writing High releases its open-drain output. The
separate VCOM `ICE_RX` and `ICE_TX` path is not part of the V1
RL78 profile because the schematic draws its shared ETM nets on two MCU pin
pairs and the missing sheets prevent a complete electrical review.

Implementation clarification: "join" does not mean short PA.9 and PA.10 at
the MCU pins or ICEJ4. Route each external jumper through its own 47-ohm
series resistor and merge the branches only at the target-side TOOL0 node.
Pull TOOL0 up to the RL78's actual VDD with 1 kohm. The Nu-Link2-Me rail powers
the RL78 at 3.3 V, so this same-domain profile does not add a level shifter.
PA.9 remains released/high impedance throughout receive and PA.10 is the GPIO
receive input.
External target power and 5 V targets remain prohibited. Because the supplied
PDF carries these nets off-page as `TICEDAT` and `TICECLK`, continuity and
isolation from the original target MCU or any other active driver remain
mandatory physical checks.

### HS USB and clocking

HS USB uses `HSUSB_VBUS` pin 43, `HSUSB_D-` pin 44, `HSUSB_VSS` pin 45,
`HSUSB_D+` pin 46, `HSUSB_VDD12_CAP` pin 47, `HSUSB_VDD33` pin 42 and
`HSUSB_VRES` pin 41. D+ and D- pass through the schematic's zero-ohm links to
the Micro-USB connector. The 12-MHz crystal is connected to PF.2/PF.3 with
20-pF capacitors to ground.

The board profile shall use the M480 BSP HSUSBD clock sequence and shall fail
USB initialization if the required HXT/PLL clocks do not become ready. The
schematic proves the crystal connection, not the final clock-tree settings;
USB enumeration and sustained-transfer HIL are release evidence.

### External package storage

The fitted serial flash is W25Q16JVSSIQ, 16 Mbit (2 MiB raw), powered at 3.3 V.
PC.0, PC.2 and PC.3 reach MOSI, clock and chip select through SN74LV2T45 level
translators; PC.1 receives MISO directly. The raw capacity does not itself
authorize a package size or layout. JEDEC identification, erase geometry,
write completion, protection bits, power-loss recovery, endurance and the
metadata/package partition must pass their own acceptance checks before
production release.

### Target voltage policy and HIL gate

The Nu-Link2-Me target interface and the RL78 target use the Nu-Link2-Me 3.3 V
rail. An external target supply is not permitted, and the V1 profile does not
support 1.8 V or 5 V operation. This avoids a second voltage domain on TOOL0
and RESET, but it does not remove the need for common ground, continuity and
contention checks before first use.

`ETMD3_SS`/PB.12 remains unconnected and firmware does not configure EADC or
apply a target-VDD interlock. `ETMD2_CK`/PB.13 also remains reserved and
unconnected. The operator must isolate every other target supply and measure
the Nu-Link2-Me-powered target rail externally before connecting TOOL0 and
RESET. ADR-0009 records this accepted no-sense policy.

The source-level default remains fail-closed, but the formal M484SIDAE Keil
HIL-candidate target defines `NULINK2ME_RL78_TARGET_APPROVED=1`. That definition
only exposes the already bounded target path; it is not a production-release
claim. Erase and program still require all runtime package/profile/integrity
checks; they do not rely on a PB.12 voltage measurement.

Physical HIL has passed HS-USB/W25Q16 online G15 programming for 20 warm runs
and 10 bridge power-cycle runs. A subsequent KEY1 offline capture found 13
complete transactions out of 21 valid handshakes: 8/13 while PA.9/PA.10 logic-
analyzer probes were attached and 5/8 after they were removed. Failed transfers
stopped inside the program data loop; the observable half showed PA.9 Low for
0.15-1.84 ms while TOOL0/PA.10 remained High. A follow-up image with a nominal
2 ms post-ACK guard completed only 3/16 attempts; 12 stopped after data units
37-244 and one stopped during mode entry. The usual measured ACK-to-next-data
interval was 1.51-1.55 ms because the guard used a millisecond scheduler. This
did not improve stability, so the fixed guard was removed. The echo check is
retained. A 2026-07-22 SRAM-cache capture with a new direct programmer-to-target
ground entered 45/45 complete handshakes, but only 12/45 jobs transmitted all
2,048 units and the final verify ACK; the other 33 terminated at varying data
units. PA.9/PA.10 were not captured, so that evidence separates entry from
burst reliability but cannot identify the failing branch.

A later 116.78-second no-probe capture completed 34/45 full programming
sequences. Seven failures were exact prefix truncations and four contained
decoder corruption; the comparable corrupt bytes were all low-to-high changes
(`A8` to `E8`, `25` to `65`, and `FB` to `FF`). The final job passed, so a
single last-result snapshot erased the evidence from the preceding failures.
This demonstrated that retaining only the latest terminal result is unsuitable
for the repeated-KEY1 stress procedure.

Before every entry attempt, the M484 platform adapter releases PA.9, disables
the PA.10 start interrupt, module-resets TIMER2, clears GPIO/TIMER/NVIC pending
state and clears its FIFO/error latch. It does not reset the whole M484 or
automatically repeat a job after erase permission. A versioned volatile failure
snapshot is captured before cleanup. Firmware `1.0.1.2` retains the newest 64
non-PASS terminal records in a sequence-numbered RAM ring; later successful jobs
do not clear them. `GET_OFFLINE_FAILURE_HISTORY` returns the ring to the GUI,
including explicit overwrite count. Physical release remains blocked until the history-enabled matrix, the
fitted MCU/3.3 V/connector evidence, reset timing, G16 programming and remaining
power-interruption cases pass. Missing schematic sheets must be replaced by
measured board-trace evidence if they cannot be obtained.

The first history-enabled capture recorded 38 offline jobs and retained all 13
non-PASS jobs with no overwrite. Job counters 8, 19, 21-24, 27-29, 31 and 36-38
matched the 13 truncated LA transactions exactly. Every failure was a
program-stage `TARGET_LINK_DIRECTION_COLLISION`; the snapshot consistently
showed PA.9 driven Low and PA.10 sampled High. This validates the history path,
but not programming stability.

Firmware `1.0.1.3` added a one-second programming-link reset. A later run
completed 9/28 jobs; all 19 failures were program-stage
`TARGET_LINK_DIRECTION_COLLISION`, and every failure was followed by the
expected reset at approximately one second. The reset policy therefore executed
correctly but could not repair the partially programmed target image.

Firmware `1.0.1.4` replaces the terminal reset-only behavior with a bounded
full-job recovery. A slow-Green indication still triggers link recovery only if
the authoritative stage remains `TARGET_CONNECT` for one second; normal Green
flashing while programming is never treated as failure. Any terminal offline
failure immediately asserts target RESET, releases TOOL0 and cleans TIMER2,
GPIO IRQ/FIFO and direction state. The target remains in RESET while failure is
shown. Only an exact `TARGET_LINK_DIRECTION_COLLISION` may start a new complete
job after one second, beginning again at handshake, erase and address zero.
Each physical KEY1 request permits two automatic retries, for three complete
attempts total. Exhaustion, cancellation, other errors and unknown outcome stay
parked until a new KEY1 request. M484 is not reset, so HSUSB is not
re-enumerated and the volatile history is preserved. This new policy is
software/build verified and remains pending physical HIL.

After a complete bridge/target power cycle, the `1.0.1.4` image completed 64/64
normal G15 8 KiB KEY1 transactions with the exact P21 payload, all 2,048 data
ACKs and the final verify ACK. No direction collision occurred, so this run does
not verify the bounded retry branch. Firmware `1.0.1.5` separately changes the
GUI/app-issued bridge reset to a full M484 chip reset as recorded in ADR-0011;
the per-job collision recovery above still does not reset M484 or erase its
volatile failure history.

### KEY1 and status LED behavior

An active-low KEY1 press requests offline programming only when a valid active
package and all destructive gates are satisfied. Otherwise the request reports
failure without touching the target. LED outputs are active-low because the
schematic sources each LED from `ICEVDD` through the 330-ohm resistor array and
the MCU sinks the current.

Table 2-4 behavior is adopted with deterministic timing:

- `Flash` repeats 125 ms on, 125 ms off while the state is active.
- `Flash Slowly` repeats 500 ms on, 500 ms off while the state is active.
- `Flash x3` and `Flash x4` are one synchronized group of three or four normal
  125 ms on/125 ms off pulses on state entry, ending off.
- `Any` is unconstrained and must not be interpreted as pass or fail.

| Operation state | ICE / PF.4 | ICP / PC.6 | Red / PB.9 | Green / PB.8 |
| --- | --- | --- | --- | --- |
| Boot | Flash x3 | Flash x3 | Flash x3 | Flash x3 |
| One adapter selected to connect | Flash x4 | Flash x4 | Flash x4 | On |
| Online, target not connected | On | Any | Off | Off |
| Online, target connected | On | Any | Off | On |
| Online, target connection failed | On | Any | Flash | On |
| During offline programming | Off | On | Off | Flash Slowly |
| Offline programming completed | On | Off | Off | Off |
| Offline programming completed, auto mode | On | On | Off | Off |
| Offline programming failed | On | Flash | Off | Off |

### Online image flow

The GUI's `Program Online` path stages and validates the immutable package as a
W25Q16 candidate before target erase. The M484 firmware then expands the exact
profile code image into a global 64 KiB SRAM cache, checks package UUID,
profile, size and CRC32, and enters target RESET only after that cache is valid.
Offline Armed boot attempts the same preload for the active package. Package
mutation invalidates the cache. RL78 program-unit reads use SRAM only; W25Q16
remains the durable authority. Host-fed online streaming, in which the PC sends
later image blocks while earlier verified blocks are programmed, is deferred
to a negotiated PLP extension. It shall not be emulated by changing PLP V1
command semantics.

That extension must define capability negotiation, bounded window ownership,
per-block integrity, replay, timeout, cancellation, host disconnect and
unknown-outcome recovery. It must also prove all profile and image checks that
authorize erase before the first destructive command. The GUI continues to
reach it only through Programmer SDK and `ITransport`.

## Consequences

The Nu-Link2-Me gains a board-specific route to online and button-triggered
offline RL78 programming while reusing the existing platform contracts. Its
2-MiB flash removes the M032 64-KiB slot as a physical dependency, but usable
image size remains profile- and storage-layout-controlled. The initial release
is deliberately limited to the measured Nu-Link2-Me 3.3 V target rail. The
formal HEX is a HIL candidate, not hardware-proven or production-ready output.

## Evidence and references

- `docs/nulink2me/Nu-Link2-Me_schematic.pdf`, page 1, Rev 1.0,
  `Sheet 1 of 3`, dated 2019-02-26.
- `docs/nulink2me/UM_Nuvoton_Nu-Link_Debug_Adapter_EN_Rev1.01.pdf`, page 6,
  section 2.4 and Figure 2-4; pages 8-9, Table 2-4.
- `demo_code/M480BSP_Nulink2Me_RL78/Library/StdDriver/inc/sys.h` for available
  M484 multi-function selections.
- Required release evidence: board continuity/voltage measurements, HS USB
  enumeration and stress test, W25Q16 JEDEC/read/write/erase/power-cut tests,
  logic-analyzer Protocol B entry and end-to-end RL78 HIL.
