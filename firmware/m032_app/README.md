# M032 application composition

`m032_application.*` combines the PLP programmer, package/mode store, target
driver, board I/O and USB port. `m032_numicro_hid_port.*` owns the EP2 interrupt
IN and EP3 interrupt OUT path, while `m032_hid_descriptors.c` defines the
vendor-specific 64-byte HID interface. `m032_iap_control.*` invalidates the
reserved application CRC page and resets into the resident bootloader only
after the Return Boot loader response has completed, with a bounded fallback.
It also provides an ACK-completed normal MCU reset that leaves the CRC page
unchanged, so the resident bootloader launches the same application again.

`m032_numicro_main.c` is the BSP entry point. It always initializes HID. An
internal storage provider is included only when the release build supplies
page-aligned `M032_INTERNAL_STORAGE_BASE`, `M032_INTERNAL_STORAGE_SIZE` and
`M032_FIRMWARE_END_ADDRESS` values derived from the linker map. The target
binding installs the portable G15/G16 Protocol B driver and advertises target
programming only when that binding initializes successfully.

The buildable BSP-style mirror is under
`demo_code/M032BSP_RL78_Programmer/SampleCode/RL78_Programmer`; its `KEIL`
project links the application at `0x00004000`, with linked content ending
before the reserved CRC page at `0x0000FA00`. The resident `ISP_HID`
bootloader occupies `0x00000000-0x00003FFF`; metadata occupies
`0x0000FC00-0x0000FFFF`, followed by one 64 KiB package slot. Run
`scripts/sync_m032_sample.ps1` after changing canonical firmware sources.
Builds that do not select the approved scatter file must not define the
internal-storage macros.
