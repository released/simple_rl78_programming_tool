# RL78 programmer sample

The complete bridge consists of:

- resident bootloader: `../ISP_HID/KEIL/ISP_HID.uvprojx`, APROM
  `0x00000000-0x00003FFF`;
- RL78 programmer application: `KEIL/Template.uvprojx`, fixed application
  image `0x00004000-0x0000FBFF`;
- CRC erase page: `0x0000FA00-0x0000FBFF`, with CRC32 at `0x0000FBFC`.

Build both projects and validate the packaged image by running:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_m032_keil.ps1
```

The package is self-contained under `M032BSP_RL78_Programmer`: all CMSIS,
device, StdDriver, application and Protocol B source paths are relative. The
command-line build validates bootloader outputs in `ISP_HID/KEIL/obj` and
application/factory outputs in `RL78_Programmer/KEIL/obj` without duplicating
them under root `build`. Use `M032Rl78Bridge_factory.hex` for initial
debugger provisioning. Later GUI updates use only the fixed `0xBC00`-byte
`M032Rl78Bridge.bin`; the USB path never replaces the resident bootloader.
The ISP boot project uses APROM algorithm `M031_AP_128.FLM`. Initial Nu-Link
ICP/Keil provisioning must set only `CONFIG0.CBS[7:6]=10` and preserve every
other CONFIG bit; an invalid CBS mode is rejected by host preflight before
application erase.
