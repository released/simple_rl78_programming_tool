# Firmware Third-party Dependencies

Reserved for approved Nuvoton BSP references and cryptographic/storage libraries with exact versions, licenses, modifications and security review. Domain modules must not depend directly on vendor BSP headers.
The NuMicro HID endpoint pattern and descriptor layout in `firmware/m032_app`
are adapted from Nuvoton's M031 BSP USB HID sample under SPDX Apache-2.0. The
vendor CMSIS/StdDriver source tree remains external and is referenced by
`scripts/build_firmware_core.ps1`; it is not copied into this repository.

No Renesas source is compiled into the firmware. The Protocol B state machine
is independently implemented from the controlled official guide; sample-source
inventory hashes and the no-copy rule are recorded under `docs/reference`.
