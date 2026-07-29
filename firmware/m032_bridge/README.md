# M032 bridge application

`m032_programmer.c` is the allocation-free PLP adapter used by the USB HID
interrupt endpoints. Integrate it with the Nuvoton BSP as follows:

1. Call `M032Programmer_Init()` after clocks, GPIO and storage are initialized.
2. Install the approved storage/target callbacks with
   `M032Programmer_Configure()` and advertise only the capabilities those
   callbacks actually implement.
3. Initialize `M032NumakerBoard`, then call `M032Programmer_AttachBoardIo()` so
   the programmer owns the sole active-low PB.14 status LED.
4. Pass each exact 64-byte interrupt OUT payload to
   `M032Programmer_OnOutReport()` from the endpoint handler.
5. Call `M032Programmer_Process()` and
   `M032Programmer_UpdateIndicator(now_ms)` in the main loop, never inside the
   ISR.
6. When `M032Programmer_TakeInReport()` returns one, copy the 64-byte report to
   the interrupt IN endpoint buffer and arm the endpoint.

The initial board profile treats an M032 external pin reset as the offline
trigger. `M032Programmer_ProcessOfflineAutorun()` starts only after a bounded
HID-session grace period; power-on reset alone does not start programming. A
HID session suppresses a pending or pre-erase handshake-retry job.

`firmware/storage` supplies the callback implementation,
`firmware/m032_app` supplies the NuMicro HID composition, and
`firmware/target` supplies the G15/G16 Protocol B state machine. No physical
APROM partition is selected unless the approved scatter file and partition
macros are supplied. Physical target release still requires ADR-0004 HIL.
