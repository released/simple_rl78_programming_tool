# ADR-0011: Bridge Bootloader and Application-Only USB HID Update

- Status: Accepted / physical HIL pending
- Date: 2026-07-13
- Supersedes: the monolithic M032 bridge-firmware partition in ADR-0003

## Context

Both supported bridge boards need a recoverable development update path so the
RL78 programming application can be replaced without using a debugger for every
release. The bootloader must remain available when an application update is
interrupted or rejected. M032 must also preserve its metadata journal and
64 KiB RL78 offline-package slot.

The existing PLP staged/signed update commands describe a future production
update architecture. The implemented development path instead hands the running
application to a small resident Nuvoton ISP HID bootloader and transfers one
fixed-layout application binary. The two paths must not be represented as the
same security or rollback capability.

## Decision

- Initial board provisioning uses a debugger/programmer to write the resident
  bootloader and application/factory image. The USB update path never erases or
  replaces the bootloader region.
- Both resident bootloaders are APROM images, not LDROM images. The M032 boot
  target uses `M031_AP_128.FLM`; the M484 boot target uses `M481_AP_512.FLM`.
  Initial provisioning must set only `CONFIG0.CBS[7:6]=10` (APROM with IAP) on
  both boards and additionally `MBS bit 5=1` on M484, using Nu-Link ICP/Keil
  configuration controls while preserving every unrelated CONFIG bit.
- The running bridge application implements PLP `FW_ENTER_BOOTLOADER` (`0x0506`).
  It is allowed only in a negotiated open session with programmer-update
  capability, no active image transfer or programming job, and application
  state `SESSION`.
- The handoff callback validates required CBS/MBS state before invalidating the
  application. An incompatible setting returns `UPDATE_PACKAGE_INCOMPATIBLE`
  and leaves the application valid.
- Before acknowledging `FW_ENTER_BOOTLOADER`, the application erases its entire
  reserved CRC page. This intentionally makes the application non-bootable.
  After the ACK has completed on USB, the board resets with APROM vector mapping
  returned to address zero. A bounded reset fallback prevents an indefinitely
  pending handoff if completion cannot be observed.
- The bootloader enumerates as Nuvoton ISP HID `VID:PID 0416:3F00`. It validates
  vectors and CRC32 before starting the application. If validation fails or an
  update is interrupted, it remains at `0416:3F00` waiting for another upload.
- The PC `FW Upload (USB HID)` workflow accepts only a raw application BIN that
  exactly matches the selected board profile. It validates the fixed size and
  little-endian CRC-32/ISO-HDLC trailer, binds the full HID path to one
  bootloader instance, then matches that profile against the bootloader's
  reported application geometry before any erase/program command. A
  selected-profile/reported-geometry mismatch fails before erase. PDID is
  probed and displayed, but is not a hard-coded allowlist. Required CBS/MBS
  configuration is also checked and a mismatch fails closed before erase. The
  Nuvoton ISP flow
  erases, programs, reads data back after each write for byte comparison, and
  checks the complete application additive checksum. `Run Application` is a
  separate explicit action after successful programming.
- Both direction changes mask interrupts before changing VECMAP: application
  handoff masks before restoring vector page zero/reset, and the bootloader
  masks before mapping vectors to `0x4000` and branching to the application.
  Each path clears/checks the FMC failure flag and confirms VECMAP by readback;
  an unconfirmed map is never used for the reset/branch.
- Bootloader FMC read/program/erase trigger polling is bounded; timeout or ISP
  failure aborts the command rather than wedging HID indefinitely.
- Bridge application BINs are not OIP target packages and do not consume or
  replace the RL78 offline package slot.

The fixed APROM layouts are:

| Board | Region | Start | End inclusive | Size / rule |
| --- | --- | ---: | ---: | --- |
| M032SE3AE | Resident bootloader | `0x00000000` | `0x00003FFF` | 16 KiB; debugger-only update |
| M032SE3AE | Application image | `0x00004000` | `0x0000FBFF` | Fixed BIN size `0xBC00` |
| M032SE3AE | Linkable application code/data | `0x00004000` | `0x0000F9FF` | CRC erase page excluded |
| M032SE3AE | Application CRC page | `0x0000FA00` | `0x0000FBFF` | Reserved 512-byte erase unit |
| M032SE3AE | Application CRC32 | `0x0000FBFC` | `0x0000FBFF` | LE CRC-32/ISO-HDLC over BIN offsets `0..0xBBFB` |
| M032SE3AE | Metadata journal | `0x0000FC00` | `0x0000FFFF` | Two 512-byte pages |
| M032SE3AE | RL78 Package slot A | `0x00010000` | `0x0001FFFF` | 64 KiB; no A/B retention |
| M484SIDAE | Resident bootloader | `0x00000000` | `0x00003FFF` | 16 KiB; debugger-only update |
| M484SIDAE | Application image | `0x00004000` | `0x0007FFFF` | Fixed BIN size `0x7C000` |
| M484SIDAE | Linkable application code/data | `0x00004000` | `0x0007EFFF` | Final 4 KiB page excluded |
| M484SIDAE | Application CRC page | `0x0007F000` | `0x0007FFFF` | Reserved 4 KiB erase unit |
| M484SIDAE | Application CRC32 | `0x0007FFFC` | `0x0007FFFF` | LE CRC-32/ISO-HDLC over BIN offsets `0..0x7BFFB` |

SRecord post-build processing fills all unused bytes in each fixed application
image with `0xFF` and writes the CRC32 trailer. A shorter linker output or an
arbitrary BIN is not a valid upload artifact.

## Consequences

An interrupted upload is recoverable by scanning `0416:3F00` and uploading the
same or another valid application BIN again. There is no application A/B
rollback, signature verification, anti-rollback policy, or bootloader self-
update in this development flow. Production deployment still requires the
separately designed staged/signed update policy and physical power-loss tests.

Erasing the M032 application CRC page does not touch metadata or Package slot A.
Updating the bridge application therefore preserves the installed RL78 offline
package unless a separate package operation changes it. Initial debugger
programming or a full-chip erase may still erase all APROM content.

On 2026-07-23, an M484 CPU-only GUI reset left autonomous KEY1 programming
unresponsive after application upload, while a complete power cycle restored
64/64 normal G15 transactions. Nu-Link2-Me firmware `1.0.1.5` therefore uses an
M484 full-chip reset for application-issued `RESET_PROGRAMMER` and
`FW_ENTER_BOOTLOADER` transitions after ACK completion or fallback. This clears
internal peripheral state while preserving the application CRC for a normal
reset. The resident bootloader's validated VECMAP handoff to the application
remains a CPU reset. External W25Q16 and target hardware remain powered.

When more than one bootloader is present, the GUI displays the complete HID
path for each descriptor and requires an explicit selection. Geometry/profile
binding remains mandatory even after the operator chooses a path.

## Evidence Required

- Automated fixed-size/CRC validation and bootloader-range rejection.
- APROM flash-algorithm audit plus CBS/MBS preflight rejection while preserving
  unrelated CONFIG bits.
- Selected-profile/reported-application-geometry rejection before erase,
  full-HID-path instance binding, per-write readback mismatch rejection and
  interrupt-masked, failure-checked VECMAP transitions.
- Bounded FMC trigger timeout/ISP-failure injection for read/program/erase.
- Debug/Release PC builds, native updater/PLP tests, 14 portable firmware files
  checked with armclang `-std=c90 -pedantic -Werror`, 6 M032 NuMicro adapters
  checked with armclang `gnu90`, and both boot/application Keil map and
  post-build image checks. M484 adapters and the two vendor-derived ISP_HID
  projects are verified by Keil builds; this is not a separate strict-C90
  claim for all vendor USB/HID sources.
- Physical enumeration and re-enumeration for application PID and
  `0416:3F00`, update interruption at erase/program boundaries, invalid-vector/
  invalid-CRC stay-in-boot behavior, and successful application launch.
- Existing HSUSB, W25Q16, TOOL0/reset waveform, and RL78 erase/program/verify
  HIL remain separate release gates.

## Current Verification

Native Debug/Release pass 222/222 checks; the prior relevant final MFC
Debug/Release builds pass with zero warnings/errors.
M032 Keil Release produces an 8.13 KiB bootloader (7.87 KiB free), 18.81 KiB
application (27.69 KiB linkable free), `0xBC00` APP BIN with CRC32 `0x50A01C63`
at `0x0000FBFC`, and a `0xFC00` factory BIN ending before
metadata at `0x0000FC00`. M484 Keil Release produces a 4.53 KiB bootloader
(11.47 KiB free), 24.08 KiB application (467.92 KiB linkable free), `0x7C000`
APP BIN with CRC32 `0xAC381304` at `0x0007FFFC`, and `0x80000` factory BIN.
Both Keil scripts exit `0` with zero
warnings/errors.

Physical application/bootloader enumeration, update interruption recovery,
the new M484 full-chip reset/re-enumeration path and post-reset KEY1 behavior
remain **NOT RUN**. Existing W25Q16, TOOL0/reset waveform and G15 programming
evidence does not close those update/reset acceptance gates.
