# Controlled RL78 Protocol B references

These files are local design inputs for the independently implemented M032
driver. They are not compiled or redistributed as firmware source.

| File | Purpose | SHA-256 |
| --- | --- | --- |
| `RL78_Protocol_B_Serial_Programming_Guide.pdf` | Normative command, status, timing and size-code flow | `0E0F1052C72038558BBECB4F3FD6A72BEBAE27A15320832C9C3663F361813CDC` |
| `RL78_Flash_Programmer_Protocol_B_Rev1.00.pdf` | Official programmer application-note context | `7092A26800935E27D1C25E11E5383DAA095F4E05F4BF8B9FFBA018A327486C34` |
| `protocol_family_mapping_faq.txt` | Local protocol-family mapping note | `AB2880ABD10B36FD1A200A981C05AEA5144F283D4759B8B5984CF203E4391C17` |

The external, authorized Renesas sample files inspected during protocol review
were not copied. The original workstation path is intentionally omitted. Their
inventory hashes are:

- `protocol_b.c`: `B7AA147D797EB97E6581495C02FFD5AA299E32DDE3E44EBF74CA62F05C60BD23`
- `protocol_b.h`: `14223A193DF68BC41F4F30F0EEB372CE57B75B06E5AB050E4605CFD7D00B8E11`

Implementation rule: derive behavior from the official programming guide;
do not copy the sample's blocking globals, generated peripheral calls, RL78
memory qualifiers, or source text.
