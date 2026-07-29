# RL78 Protocol B target driver

`rl78_protocol_b.*` is an allocation-free, nonblocking strict-C90 state
machine independently implemented from the official serial programming guide.
`m032_rl78_target_binding.*` adapts it to M032 board I/O and UART1.

Implemented profiles:

- RL78/G15 4 KiB and 8 KiB Code Flash.
- RL78/G16 16 KiB and 32 KiB Code Flash.
- Option bytes `0xC0-0xC3` and security ID `0xC4-0xCD` are included in the
  full Code Flash stream.

The target size code must exactly match the selected OIP profile before erase
permission is sent. G15/G16 Data Flash programming is rejected because the
official Protocol B guide does not support command `0x63` for those families.

Offline mode retries only mode handshaking every 500 ms. It does not
automatically retry after erase permission or any unknown destructive outcome.
Physical release remains gated by target-board electrical and HIL evidence.

Controlled local references and hashes are in
`docs/reference/renesas_rl78_protocol_b/README.md`.
