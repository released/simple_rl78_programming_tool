# Hardware-in-loop Tests

Physical tests are not complete. Do not mark an item PASS without retaining the
board revision, MCU marking, target orderable part, selected profile, firmware
hash, image hash, measured result and logic-analyzer or readback evidence.

## Nu-Link2-Me M484SIDAE and RL78/G15

Use `demo_code/M480BSP_Nulink2Me_RL78/SampleCode/Template/Keil/obj/NuLink2Me_boot_app.hex`
for initial SWD
provisioning. The fixture must route PA.9/`SWDH_DAT`/ICEJ4-3 transmit and
PA.10/`SWDH_CLK`/ICEJ4-5 receive through separate external 47-ohm resistors,
then join them at RL78 TOOL0. Pull TOOL0 up to the actual 3.3 V target VDD with
1 kohm, connect PF.5 to RESET#, Nu-Link2-Me 3.3 V to RL78 VDD, and connect
common ground. Leave PB.12/ETMD3_SS and PB.13/ETMD2_CK unconnected. External
target power and 5 V are prohibited. Before power-up, confirm that off-page
`TICEDAT`/`TICECLK` paths are isolated from the original target MCU and any
other active driver.

1. `HIL-NL2-001`: verify the fitted marking is M484SIDAE and record the idle
   3.3 V, RESET#, TOOL0 and ground voltages before flashing.
2. `HIL-NL2-002`: flash the bridge HEX and verify HS-USB enumeration as the
   64-byte HID programmer. Record VID/PID, strings and sustained report traffic.
3. `HIL-NL2-003`: verify W25Q16 JEDEC `EF 40 15`, sector erase, page write,
   readback, package finalize, activation and recovery after power interruption.
4. `HIL-NL2-004`: confirm PB.12 remains unused and programming mode does not
   require a PB.12 connection. Measure and record target VDD externally before
   connecting TOOL0/RESET because firmware no longer provides this interlock.
5. `HIL-NL2-005`: with the exact G15 4 KiB or 8 KiB profile selected, load a
   known MOT/S-record or raw BIN (`0x00000000` base), click `Program Online`,
   and verify that W25Q16 candidate validation precedes target erase.
6. `HIL-NL2-006`: capture RESET#/TOOL0 entry timing, 115200-bps 8-O-2 transmit,
   8-O-1 receive and PA.9 high-impedance turnaround. Independently read back or
   execute the G15 image before recording PASS.
7. `HIL-NL2-007`: run `Download`, `Activate`, and `Arm Offline`; disconnect the
   GUI session while retaining USB power, then press KEY1. Confirm the active
   W25Q16 image programs and verifies the same target without HID commands.
8. `HIL-NL2-008`: press KEY1 with the target absent. Confirm pins are released
   and handshaking repeats every 500 ms; connect the valid target and confirm
   programming proceeds. Confirm the millisecond clock, debounce and LEDs keep
   advancing during the retries. During one 500 ms wait, press KEY1 again and
   confirm a new RESET/TOOL0/`0x3A` sequence begins immediately after debounce,
   without waiting for the old deadline or creating a second job. After erase
   begins, confirm another KEY1 press does not restart or interrupt the job.
9. `HIL-NL2-009`: reconnect the GUI while OFFLINE ARMED, verify the displayed
   mode, switch to ONLINE ONLY, and confirm the stored package is preserved.
10. `HIL-NL2-010`: repeat online and KEY1 flows across minimum/nominal voltage,
    erased/non-erased targets and the approved G15 part matrix. Exercise power
    loss before erase, during SPIM replacement and after target erase separately.
11. `HIL-NL2-011`: with only production connections (`VDD`, `TOOL0`, `RESET#`,
    `GND`) present and no PA.9/PA.10 logic-analyzer probes, run at least 20
    consecutive G15 8 KiB KEY1 jobs. Require every job to contain 2,048 complete
    four-byte data ACKs plus final verify ACK. Confirm the active OIP is fully
    cached before RESET and that no W25Q16 reads occur between target entry and
    terminal result. Then force an open PA.9 branch and a driven-High TOOL0
    contention separately; require bounded termination as
    `TARGET_LINK_DIRECTION_COLLISION (0x20907003)` with PA.9 released. Do not
    insert a fixed millisecond post-ACK guard.

## M032 and Other Profiles

The M032 EVB matrix still requires exact G15/G16 target tests, externally
measured target voltage, PB.3-NC verification, TOOL0/reset timing analysis,
reset-triggered offline operation, power interruption, LED behavior, endurance
and comparison against an independent reference programmer. Firmware performs
no M032 target-VDD read/range gate.

## M032 TEST BOARD

Use
`demo_code/M032BSP_RL78_Programmer/M032_TEST_BOARD_WIRING.md` and initially
provision
`SampleCode/RL78_Programmer/KEIL/obj/M032TestBoardBridge_factory.hex`.

1. `HIL-MTB-001`: verify 3.3 V VCOM PF.2/PF.3 at 115200 8-N-1, connect through
   the GUI board profile, confirm product identity `M032 TEST BOARD`, and
   confirm a single 64-byte response completes without waiting for a larger
   host read buffer.
2. `HIL-MTB-002`: inject leading noise, a truncated PLP frame and a bad CRC;
   require bounded recovery on the next complete 64-byte report.
3. `HIL-MTB-003`: verify A11/PB.10, A10/PB.9 and A9/PB.8 continuity, the fitted
   30-kohm pull-ups, separate 47-ohm TOOL0 branches and common ground.
4. `HIL-MTB-004`: capture PB.8 RESET and TOOL0 mode entry, 115200 8-O-2
   transmit, 8-O-1 receive, open-drain release and direction collision.
5. `HIL-MTB-005`: program G15 4/8 KiB online and verify execution/readback.
6. `HIL-MTB-006`: program G16 16/32 KiB online and verify execution/readback.
7. `HIL-MTB-007`: download, activate and arm an OIP in internal APROM; remove
   GUI communication, press PC.2 and verify one complete offline job.
8. `HIL-MTB-008`: repeat PC.2 offline jobs, absent-target retries, failure
   parking, power cycles and interrupted package replacement.
9. `HIL-MTB-009`: exercise both Application `Return Boot loader` and PC.2-held
   reset entry, confirm the Application COM handle is released, Scan lists
   only the UART ISP that reports `MTB1`, and reject an App-mode/unrelated COM,
   a missing/wrong board ID and any non-`0xBC00` image.
10. `HIL-MTB-010`: interrupt UART ISP erase/program at multiple offsets,
    reconnect on the same COM port and successfully repeat the full update.
11. `HIL-MTB-011`: verify PA.11 Red and PA.10 Green indications for ready,
    program, PASS, FAIL and UART ISP.
12. `HIL-MTB-012`: measure HIRC/UART0 baud and TIMER2 417-tick TOOL0 timing
    across the approved voltage and temperature range before production use.
