# M032 TEST BOARD UART ISP

This bootloader is the UART counterpart of `ISP_HID` for the M032 TEST
BOARD. It is intentionally restricted to the fixed application image range
`0x00004000` through `0x0000FBFF`.

## Connection

- JP22 pin 3: GND
- JP22 pin 5: 3.3 V reference
- JP22 pin 6 / `MCU_RX` / PF.2: connect to the VCOM TX output
- JP22 pin 7 / `MCU_TX` / PF.3: connect to the VCOM RX input
- UART: 115200 baud, 8 data bits, no parity, 1 stop bit

Use only a 3.3 V TTL VCOM adapter. This is not an RS-232 electrical
interface.

## Boot selection

- Hold PC.2 low while resetting or powering the board to force UART ISP.
- A valid application with a correct CRC32 is started automatically when
  PC.2 is released.
- The application `Return Bootloader` command invalidates its application
  checksum before reset, so UART ISP remains active.

The `CONNECT` reply includes board ID `0x3142544D` (`MTB1`). The GUI checks
this identity before erasing APROM.
