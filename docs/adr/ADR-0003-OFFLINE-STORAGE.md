# ADR-0003: Offline Storage Provider and Partition

- Status: Accepted / firmware partition superseded by ADR-0011; physical
  endurance and HIL evidence pending
- Date: 2026-07-11

## Context

M032SE3AE has 128 KB APROM and 16 KB SRAM. Firmware, durable records and
offline images compete for Flash. The product requires one offline package up
to 64 KiB and accepts losing the previous package when its replacement starts.

## Proposed Decision

This ADR originally selected the upper 65 KiB of M032SE3AE APROM as an internal
single-slot store and kept the lower 63 KiB as one monolithic bridge-firmware
region. ADR-0011 preserves the storage addresses but divides that lower region
between a resident bootloader, fixed application image and application CRC
page. The original layout was:

| Region | Start | End inclusive | Size |
| --- | ---: | ---: | ---: |
| M032 bridge firmware | `0x00000000` | `0x0000FBFF` | 63 KiB |
| Metadata journal page 0 | `0x0000FC00` | `0x0000FDFF` | 512 B |
| Metadata journal page 1 | `0x0000FE00` | `0x0000FFFF` | 512 B |
| Package slot A | `0x00010000` | `0x0001FFFF` | 64 KiB |

ADR-0011 now owns the boot/application linker and image limits. The store retains
the two-page generation/CRC metadata journal, but it advertises one slot and no
A/B capability. Before slot A is erased for a replacement, the old active
identity is durably invalidated. A reset or power loss during replacement thus
leaves no active offline package and requires a new GUI download.

Storage remains behind `IStorage`. A future RL78 package above 64 KiB or a
previous-package rollback requirement needs external serial storage or a
higher-capacity programmer MCU. Growth of bridge application code is governed
by the separate ADR-0011 application boundary.

## Information Required

- Final linked bootloader and application sizes and release margin within the
  ADR-0011 regions.
- External device/interface/BOM and power-loss electrical behavior.
- Erase endurance and station lifetime target.

## Acceptance Evidence

Approved memory map, linker/map margin, worst-case capacity calculation, erase/write timing, endurance model and power-cut fault test.

## Implementation Progress

The storage-independent implementation now provides one 64 KiB package slot,
two-page generation/CRC journal recovery, streaming SHA-256 and canonical OIP
CRC32 validation, journaled activation, active-image reads and persistent
operating-mode records. The project-local boot/application scatter files enforce
the ADR-0011 regions. The NuMicro FMC backend still requires explicit release-target
defines, so a build that does not select the approved scatter file remains
fail-closed. The formal Keil Release target and map are now available; release
approval still requires physical power-cut/endurance evidence. An external NOR
backend can use the same interface
without changing the programming core.

Historic monolithic-image size results in this ADR do not describe the current
boot/application build. Current build evidence is recorded in `SELF_CHECK.md`;
ADR-0011 remains the authority for application size, reserved CRC page and
bootloader boundary.
