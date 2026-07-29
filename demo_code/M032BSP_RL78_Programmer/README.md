# M032 BSP package for the RL78 programmer

This directory follows the same embedded-project layout as the referenced
PMBus/SMBus and SMBus/SGPIO workspaces:

```text
M032BSP_RL78_Programmer/
|- Library/
|  |- CMSIS/
|  |- Device/Nuvoton/M031/
|  `- StdDriver/
`- SampleCode/
   |- ISP_HID/
   |  `- KEIL/ISP_HID.uvprojx
   `- RL78_Programmer/
      |- firmware/
      |- shared/
      `- KEIL/Template.uvprojx
```

`Library` is the controlled NuMicro BSP snapshot. `SampleCode/ISP_HID` is the
resident 16 KiB USB HID bootloader, while `SampleCode/RL78_Programmer` is the
application linked at APROM `0x00004000`. The application mirror uses only
relative paths inside this package. The architecture-owned sources remain
under the repository `firmware/` and `shared/` directories; run
`scripts/sync_m032_sample.ps1` after changing them and before committing or
building the Keil samples.

The bootloader is an APROM target and its active Nu-Link configuration uses
`M031_AP_128.FLM`, not an LDROM algorithm. Before initial factory-image
programming, set only `CONFIG0.CBS[7:6]=10` (APROM with IAP) through Nu-Link
ICP/Keil and preserve all unrelated CONFIG bits. The GUI updater validates this
required state and fails closed before erase when it is wrong.

Board-to-target connections are documented in [WIRING.md](WIRING.md).
