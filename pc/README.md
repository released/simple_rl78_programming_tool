# PC implementation

`app/` contains the native MFC application. `sdk/` contains the reusable
Programmer SDK and the only Win32 HID access in the PC implementation.

The current GUI supports discovery, connection/session negotiation, Motorola
S-record validation, raw BIN mapping from Code Flash `0x00000000` against the
selected target profile, deterministic OIP generation, package download/activation,
online/offline programming jobs, safe-point cancellation, progress, history and
logging. Configuration is stored in a portable INI file beside the executable;
the application does not depend on Registry configuration.

Program and Offline share one target-profile and MOT/HEX/BIN input. Offline
automatically builds the OIP during `Load + Validate`, then exposes byte
progress and five state rows for board-storage download, activation, arming and
physical triggering. Activation/arming remain disabled unless the downloaded
and bridge-reported active package UUIDs match that validated source.

`FW Upload (USB HID)` is implemented through the SDK, not through direct GUI
HID access. It validates the fixed M032 or M484 application BIN and CRC32,
enters the resident bootloader through the running PLP application, enumerates
Nuvoton ISP HID PID `0x3F00`, programs only APROM from `0x00004000`, verifies
the whole-image additive checksum and then explicitly runs the CRC-gated
application.

Build through the root `Rl78ProgrammerTool.sln` or `scripts/build_mfc.ps1`.
