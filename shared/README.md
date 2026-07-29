# Shared portable implementation

This directory implements the wire and image contracts used by both native
tests and the PC application:

- CRC-16/CCITT-FALSE, CRC-32/ISO-HDLC and SHA-256.
- Fixed 64-byte PLP encode/decode and response framing.
- OIP header, section and integrity validation.
- Strict Motorola S-record parsing with duplicate/overlap checks.
- Profile-driven address classification and deterministic OIP construction.
  The installed baseline is RL78/G15 8 KB; new exact G15/G16/G22/G23 maps are
  supplied as `TargetProfile` data without modifying the builder.

Numeric contract definitions are centralized in
`include/rl78_programmer/plp_contract.h` and remain synchronized with the
normative protocol and contract documents.
