# Nu-Link2-Me RL78 bridge

This directory contains the M484SIDAE Nu-Link2-Me product binding for the
shared PLP, OIP, package-store, and RL78 Protocol B implementation.

Implemented in the first vertical slice:

- 12 MHz HXT and M480 HSUSBD initialization.
- Fixed 64-byte HID IN/OUT reports using VID/PID `0416:5020`.
- W25Q16JVSSIQ 2 MiB SPIM storage on PC.0 through PC.3 with JEDEC validation
  and readback after erase/write.
- Active-low KEY1 debounce and button-triggered offline programming.
- Active-low ICE/ISP/Red/Green LED behavior derived from Nu-Link Table 2-4.
- TIMER2-driven GPIO software UART: PA.9/`SWDH_DAT` is open-drain transmit and
  PA.10/`SWDH_CLK` is falling-edge/mid-bit sampled receive. Separate external
  47-ohm branches merge at TOOL0, with a 1-kohm pull-up to the actual 3.3 V
  target VDD; PF.5 drives target reset.
- No fixed millisecond delay follows a four-byte target ACK. While PA.9 drives
  Low, PA.10 checks the merged TOOL0 echo; a High observation releases TX and
  reports `TARGET_LINK_DIRECTION_COLLISION (0x20907003)`.
- A 64 KiB SRAM image cache expands the selected G15/G16 code image before
  target RESET. UUID, profile, code size and CRC32 identify the cached image;
  download/finalize/activate invalidates it, and target programming reads SRAM.
- Firmware `1.0.1.5` keeps a 64-record non-PASS RAM history and applies a
  one-second programming-link recovery. A job stalled in `TARGET_CONNECT`
  resets target RESET/TIMER2/TOOL0 state and immediately retries the pre-erase
  handshake. A terminal offline failure immediately releases TOOL0, cleans the
  same peripheral state and holds target RESET. Only
  `TARGET_LINK_DIRECTION_COLLISION` waits one second and then starts a new full
  handshake/erase/address-zero/program/verify job. Each physical KEY1 permits
  two automatic retries (three total attempts); exhaustion, cancellation,
  other errors and unknown outcomes remain parked for a new KEY1 request.
  Normal Green programming activity never starts recovery, and M484/HSUSB is
  not reset.
- PB.12/ETMD3_SS is NC and target-VDD sensing is disabled. PB.13/ETMD2_CK is
  also reserved and unconnected.
- Application CRC invalidation after the app-side Return Boot loader command,
  followed by reset to the resident `ISP_HID_20` bootloader.
- ACK-completed normal MCU reset that preserves application CRC. Firmware
  `1.0.1.5` uses an M484 full-chip reset so internal GPIO, TIMER, SPIM and
  HSUSBD state is cleared before the resident bootloader validates and starts
  the same application. The bootloader-to-application VECMAP transition remains
  a CPU reset; external W25Q16 and target hardware are not power-cycled.

The RL78 target side uses the Nu-Link2-Me 3.3 V rail; do not connect an
external target supply or a 5 V target. The source-level target gate defaults
to zero, while the formal Keil target sets it to one for physical HIL. That HEX
is a HIL candidate. Physical HSUSB, W25Q16, TOOL0/reset and online G15 8 KiB
erase/program/verify passed 20 warm and 10 bridge power-cycle runs. A later
pre-guard KEY1 capture completed 13/21 attempts. The later nominal 2 ms-guard
image completed only 3/16, so that delay was removed and W25Q16 reads were
moved out of the time-sensitive target phase through the SRAM cache. This
was followed by a history-enabled 25/38 run whose 13 LA truncations matched 13
recorded program-stage direction collisions. The former link-reset-only image
then completed 9/28; all 19 failures were direction collisions and all 19
one-second resets appeared in LA. After a complete bridge/target power cycle,
the `1.0.1.4` image completed 64/64 KEY1 transactions with the exact G15 P21
8 KiB payload, all 2,048 data ACKs and the final verify ACK. No collision retry
was exercised. Firmware `1.0.1.5` changes the GUI reset path from CPU-only to
full M484 chip reset; its reset/re-enumeration and subsequent KEY1 behavior,
forced collision retry, PB.12 NC measurement, G16, update interruption and
LED-specific acceptance remain pending physical HIL.

`Program Online` first downloads and validates an OIP candidate in W25Q16,
expands its target code into SRAM and then programs RL78 from SRAM. For offline operation, use the GUI to download,
activate and arm the package; disconnect or close the HID session while keeping
USB power, then press KEY1 to start programming from W25Q16.

The resident bootloader occupies APROM `0x00000000-0x00003FFF`. The bridge
application starts at `0x00004000`, reserves the final 4 KiB APROM page, and
stores its CRC32 at `0x0007FFFC`. Build both projects with
`scripts/build_nulink2me_keil.ps1`. Initial debugger provisioning uses
`demo_code/M480BSP_Nulink2Me_RL78/SampleCode/Template/Keil/obj/NuLink2Me_boot_app.hex`;
later GUI updates use only `NuLink2MeRl78Bridge.bin` from that `obj` directory
and never replace the bootloader.
