# Firmware implementation

`core/` is an allocation-free, hardware-independent C90 implementation of PLP
session handling, image transfer dispatch, online/offline job arbitration,
cancellation and authoritative terminal results.

`m032_bridge/` adapts exact 64-byte USB HID reports to the core through bounded
single-entry OUT/IN queues and provides reset-triggered offline entry points.
Online Only and Offline Armed are persistent policies; HID remains available in
both.

`storage/` implements a backend-independent single-slot package store with
two-page CRC/generation journal recovery, streamed OIP integrity validation,
journaled activation and mode persistence. Its NuMicro FMC backend is enabled
only with an explicit linker-approved APROM base, size and firmware-end address.

`m032_app/` composes board, storage, target and USB ports. It contains the
vendor-defined 64-byte HID descriptor, EP2/EP3 interrupt adapter, application
CRC invalidation/normal-reset control and a NuMicro main entry. The normal
programmer reset preserves CRC, while Return Boot loader invalidates it. The
application is linked at `0x00004000`; its resident USB HID bootloader is maintained under
`demo_code/M032BSP_RL78_Programmer/SampleCode/ISP_HID`. `target/` contains the
nonblocking G15/G16 Protocol B driver and the M032 transport binding.

`board/` contains the accepted NuMaker-M032SE pin registry, portable entry
sequence and the NuMicro register adapter for same-side Arduino signals UART1
PA.3/PA.2, reset PA.5, external power enable PA.4 and the sole active-low PB.14
upgrade indicator. PB.3 is NC and target-VDD sensing is disabled.

`nulink2me/` is the second board adapter. It composes M484SIDAE HSUSBD,
W25Q16 SPIM package storage, active-low KEY1, the ICE/ISP/Red/Green indicator,
and a TIMER2-driven PA.9 open-drain GPIO TX/PA.10 GPIO RX plus PF.5 reset RL78
transport. Before target entry, its 64 KiB SRAM cache expands the selected
G15/G16 code image from W25Q16 and validates package UUID, profile, size and
CRC32; the four-byte programming loop then performs SRAM reads only. Package
mutation invalidates the cache. The board adapter also checks every PA.9 Low
through PA.10 so an open branch or opposing High drive terminates as a direction collision. It owns the
application CRC invalidation/reset control used by `PLP_CMD_FW_ENTER_BOOTLOADER`
and the CRC-preserving reset used by `PLP_CMD_RESET_PROGRAMMER`.
It supplies a distinct device identity while reusing the same PLP, package
store and Protocol B core. PB.12 is NC, target-VDD sensing is disabled, and
PB.13 remains reserved. The source default is fail-closed; the formal
M484SIDAE target explicitly enables the bounded path as a physical-HIL
candidate.

The default application build enables the four reviewed Protocol B profiles.
The APROM storage partition remains opt-in through the approved linker map and
explicit macros. Physical target use still requires the board electrical/HIL
approval recorded by ADR-0004/0007.

`demo_code/M032BSP_RL78_Programmer` contains the self-contained BSP layout used
by Keil: `Library/{CMSIS,Device,StdDriver}` and
`SampleCode/{ISP_HID,RL78_Programmer}`. The application mirror is synchronized
from these canonical modules by `scripts/sync_m032_sample.ps1`. Generated Keil
output remains in each project's `KEIL/obj` and `KEIL/lst` directories.

`demo_code/M480BSP_Nulink2Me_RL78` contains the self-contained M484 project.
Use `scripts/sync_nulink2me_sample.ps1` and
`scripts/build_nulink2me_keil.ps1`; generated output remains in each project's
`KEIL/obj` and `KEIL/lst` directories.

Run `scripts/build_firmware_core.ps1` to compile the 14 portable firmware files
for Cortex-M0 with ARM Compiler 6 using `-std=c90 -pedantic -Werror`. The same
script compiles six M032 NuMicro/CMSIS adapter files at the vendor-compatible
`gnu90` boundary. M484 adapters and the M032/M484 vendor-derived ISP_HID
projects are verified by their Keil builds; no separate strict-`-std=c90`
result is claimed for all vendor USB/HID sources.
