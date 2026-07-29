# Nu-Link2-Me 與 RL78 接線說明

本接線適用於 M484SIDAE Nu-Link2-Me RL78 bridge firmware。此配置由
Nu-Link2-Me 的 `3.3V` 電源供應 RL78/G15 target board；接線前必須先隔離
target board 上的其他電源，避免兩組電源互相回灌。

## 接線表

| 功能 | Nu-Link2-Me signal | M484SIDAE pin | RL78 target | 接法 |
| --- | --- | --- | --- | --- |
| TOOL0 transmit branch | `SWDH_DAT` / GPIO software TX / ICEJ4 pin 3 | PA.9 | `TOOL0` | 由 ICEJ4 pin 3 引出，在外部導線上串聯 `47 Ω` 後接到 target-side TOOL0 node |
| TOOL0 receive branch | `SWDH_CLK` / GPIO software RX / ICEJ4 pin 5 | PA.10 | `TOOL0` | 由 ICEJ4 pin 5 引出，在外部導線上串聯 `47 Ω` 後接到同一個 target-side TOOL0 node |
| TOOL0 pull-up | Nu-Link2-Me `3.3V` | - | `TOOL0` | `1 kΩ` pull-up 必須接到 RL78 實際使用的 VDD；本配置為 Nu-Link2-Me 3.3 V |
| Target reset | `RESET` / open-drain GPIO | PF.5 | `RESET#` | 直接連接；pull-up 應位於 RL78 的 3.3 V domain |
| Target power | `VCC` | Nu-Link2-Me `3.3V` | `VDD` | 由 Nu-Link2-Me 供應 3.3 V |
| Common reference | `GND` | GND | `GND` | 必須共地 |
| Reserved | `ETMD3_SS` | PB.12 | - | `NC`；firmware 不執行 VDD sense |
| Reserved | `ETMD2_CK` | PB.13 | - | `NC` |

`docs/nulink2me/Nu-Link2-Me_schematic.pdf` PDF page 1（圖面標示 Sheet 1 of
3）確認 PA.9 / `SWDH_DAT` 直接 ICEJ4 pin 3，PA.10 / `SWDH_CLK`
直接 ICEJ4 pin 5；這兩段板上走線之間沒有串聯電阻。因此無須割線，
可將兩顆 `47 Ω` 電阻分別串在 ICEJ4 pin 3 與 pin 5 引出的外部導線上，
再將電阻的 target-side 端匯合。電阻應盡量靠近 ICEJ4，不可先直接
短路 ICEJ4 pin 3 與 pin 5 再串單顆電阻。

注意 schematic 中的 `ICE_DAT` / `ICE_CLK` 是 M484SIDAE PF.1 / PF.0 的
另一組 net，且連到 DEBUG header 與 ICER2 / ICER4 10 kΩ pull-up；它們
不是本 RL78 bridge 使用的 PA.9 / PA.10，不可混用。ICEJ4 在圖上標為
`HEADER 2.54 5X2 (NC)`；若實體未焊 connector，可由對應 pad 焊出。

`SWDH_DAT` 與 `SWDH_CLK` 還分別透過 off-page connector 延伸為
`TICEDAT` 與 `TICECLK`。使用已從原始 NuMaker target 區分離的
Nu-Link2-Me 時，上電前仍應用 continuity measurement 確認這兩路沒有
連到原始 target MCU 或其他主動驅動器。

## TOOL0 passive Y interface

PA.9 與 PA.10 **不可在 M484SIDAE 或 ICEJ4 端直接短路**。本 prototype
起始配置建議兩個 branch 各自經過一顆 `47 Ω` 串聯電阻，然後才在
target-side 端匯合為
單一 `TOOL0` node：

```text
PA.9 / SWDH_DAT / ICEJ4-3 / TX ----[47 Ω]----+
                                                   +---- RL78 TOOL0
PA.10 / SWDH_CLK / ICEJ4-5 / RX ---[47 Ω]----+
                                                   |
Nu-Link2-Me 3.3V -------------------[1 kΩ]----+

PF.5 / RESET (open drain) ---------------- RL78 RESET#
Nu-Link2-Me 3.3V -------------------------- RL78 VDD
Nu-Link2-Me GND ---------------------------- RL78 GND
PB.12, PB.13 ------------------------------- NC
```

Firmware 將 PA.9 固定配置成 GPIO open-drain；輸出 `0` 時拉低，輸出 `1`
時釋放成 high-impedance，不會 push-pull drive High。PA.10 固定為 GPIO input，
只負責接收。因此兩個串聯電阻加上 target-side 匯流點，就是本配置所稱的
passive Y interface；在 Nu-Link2-Me 與 RL78 都使用同一個 3.3 V domain 時，
不需要額外的 level shifter。

`47 Ω` 是本 passive Y fixture 的 source-damping 起始值，不是 RL78 規定的
TOOL0 pull-up 值，也不足以保護長時間的 push-pull contention。RL78 規定的
`1 kΩ` 是 TOOL0 到 target VDD 的 pull-up。不可將 branch 串聯電阻改為
`1 kΩ`；它會與 `1 kΩ` pull-up 形成過大的分壓並損害 low-level margin。
`47 Ω` 仍須透過實體 TOOL0 waveform 與 erase/program/verify HIL 驗證。

## GPIO software UART timing

目前 bridge firmware 不再將 PA.9／PA.10 切換為 UART1／UART6 peripheral：

- TIMER2 使用外部 12 MHz HXT，`104` ticks/bit，實際 baud 約
  `115384.6`（相對 115200 約 `+0.16%`）。
- PA.9 送出 programmer-to-RL78 的 `8 data / odd parity / 2 stop bits`。
- PA.9 在第一個 stop bit 開始時即釋放，同時啟用 PA.10 falling-edge IRQ；
  若 RL78 在第二個 stop-bit timer 完成前開始回覆，PA.10 IRQ 會接管 TIMER2
  並進入 RX start-bit 取樣，避免遺失最長僅 `1 µs` 的官方 `tDRT` turnaround。
- PA.10 偵測 start bit 後，先在半 bit 中點確認 Low，再由 TIMER2 逐 bit
  中點取樣 RL78-to-programmer 的 `8 data / odd parity / 1 stop bit`。
- PA.10 收到的資料先放入 8-byte software FIFO，避免 main loop 尚未輪詢時
  遺失連續的 ACK／size response。
- 每一組 4-byte program data 收到 `0x06` ACK 後可直接進入下一組，不插入固定
  millisecond guard。實測 nominal 2 ms guard 只完成 3/16 次，因此已移除。
- 進入 RESET/TOOL0 programming sequence 前，M484 會先將目標 code image
  正規化並預載到 64 KiB SRAM cache；program data loop 不再讀取 W25Q16。
- 每次 job／handshake retry 在進入 RESET/TOOL0 sequence 前，都會先釋放 PA.9、
  關閉 PA.10 start IRQ、module-reset TIMER2、清除 GPIO/TIMER/NVIC pending state，
  並清空 RX FIFO 與 error latch，確保不是沿用上次失敗的 timer/RAM link state。
  此流程不會 reset 整顆 M484，因此 HSUSB session 與 failure evidence 不會被清掉。
- Offline handshake 失敗後仍會每 500 ms 自動重試。在尚未 erase 的
  connect/retry 階段再按 KEY1，debounce 完成後會立即安全釋放目前傳輸，
  跳過剩餘 wait，重跑 RESET -> TOOL0 entry -> `0x3A` handshake；同一個
  offline job 與 attempt history 會保留。進入 erase/program 後不接受 KEY1
  restart，以免中斷 destructive operation。
- 慢閃 Green 是 offline job active，不可直接視為 failure。只有 job 持續停在
  `TARGET_CONNECT` 滿 1 秒時，才會執行 target RESET、TIMER2、PA.9／PA.10
  IRQ/FIFO reset，並立即重跑 erase 前 handshake。erase/program/verify 期間的
  Green 閃爍不會觸發此流程。
- 任一 terminal offline failure 都會立即釋放 TOOL0、清理 TIMER2／IRQ／FIFO，並
  保持 G15 RESET。只有 `TARGET_LINK_DIRECTION_COLLISION` 會在 failure 燈號保留
  1 秒後，建立新 job 並從 handshake、erase、address 0、program、verify 全部重跑。
  每次實體 KEY1 最多自動重試 2 次（合計 3 次完整 attempt）；重試耗盡、cancel、
  其他 error 與 `OUTCOME_UNKNOWN` 都保持 parked，等待下一次 KEY1。此流程不重開
  整顆 M484，因此 HSUSB 不需重新 enumeration，64 筆 volatile failure history
  也會保留。
- 驗證自動 retry 時只按一次 KEY1；若前兩次發生 collision，LA 應看到每次間隔約
  1 秒後從 RESET／handshake 重新開始，而且 history 會產生新的 job counter。等到
  PASS 或第三次 FAIL 鎖定後，才再按下一次 KEY1。
- PA.9 傳送 Low bit 時會在 bit 尾端以 PA.10 檢查 TOOL0 echo；若 PA.9 已拉低
  但 PA.10 仍為 High，立即以 `TARGET_LINK_DIRECTION_COLLISION (0x20907003)`
  安全中止，不再繼續送出不完整 frame。
- GPA/TIMER2 IRQ 優先於 HSUSB IRQ；ISR 只處理 bit timing 與 FIFO，不執行
  Protocol B 或 destructive operation。

上述 software timing 已通過 Keil compile/link。PA.9/PA.10/TOOL0 waveform、
turnaround、parity/framing 與 RL78 erase/program/verify 仍須使用 logic
analyzer／oscilloscope 對最新 bridge BIN 完成重複 HIL。

## Bootloader 與 application firmware

Bridge firmware 採 resident bootloader 加 application 的配置：

- Bootloader 位於 APROM `0x00000000-0x00003FFF`，正常的 GUI firmware update
  不會覆寫此區。
- Application 從 APROM `0x00004000` 開始。
- 首次以 SWD 建立整板映像時，可使用
  `SampleCode/Template/Keil/obj/NuLink2Me_boot_app.hex`。
- 單獨燒錄 resident bootloader 時，使用
  `SampleCode/ISP_HID_20/KEIL/obj/NuLink2MeIapBoot.hex`。
- GUI 的 `FW Upload (USB HID)` 使用固定長度且已附 CRC32 的 application image：
  `SampleCode/Template/Keil/obj/NuLink2MeRl78Bridge.bin`。

Application 執行時可由 GUI 執行 `Return Boot loader`；firmware 會抹除 application
CRC validity marker，reset 後 resident bootloader 會留在 USB HID ISP mode 等待重新
上傳 application。更新成功後再由 GUI 執行 `Run Application`。若更新途中斷電，
bootloader 仍保留，重新連線並再次上傳 application 即可。

## 驗證狀態

2026-07-20 的 HIL 已確認 application HSUSB enumeration、W25Q16 online path、
兩條 `47 Ω` branch continuity、穩定的 3.3 V VDD、RESET/TOOL0 entry，以及
G15 8 KiB online erase/program/internal-verify。online matrix 曾完成 20/20 warm
與 10/10 bridge power-cycle runs。

`DSLogic PLus-la-260720-173121.csv`／`decoder--260720-173121.csv` 的 KEY1
offline capture 共辨識 21 次有效 handshake：PA.9／PA.10 LA probes 接上時為
8/13 完整、移除 probes 後為 5/8 完整，合計 13/21。失敗均發生在 program
data loop；前段可直接看到 PA.9 已拉 Low `0.15-1.84 ms`，但 TOOL0／PA.10
仍為 High。因兩段成功率相近，不能將 probes 視為主要原因。後續帶 nominal
`2 ms` guard 的 capture 只完成 3/16 次，12 次停在 program data unit 37-244，
另 1 次停在 mode entry；guard 已移除。

`DSLogic PLus-la-260722-100522.csv`／`decoder--260722-100522.csv` 使用 direct
Nu-Link2-Me-to-G15 GND 且未量 PA.9／PA.10。45/45 次都進入完整 handshake，
但只有 12/45 次完成 2,048 個 unit 與最後 verify ACK；其餘 33 次在不同 data
unit 中止。因此 direct GND 已改善 entry，仍未解決 data burst 穩定度。

後續 116.78 秒 no-probe capture 已完成 34/45 次 programming；最後一次 PASS 會把
原本的 single-result failure snapshot 覆蓋，因此該設計不適合連續 KEY1 stress test。

`FW 1.0.1.5` 除 SRAM cache、TX echo/collision detection 與上述 recovery 外，
會在 RAM 保留最新
64 筆 non-PASS offline history；後續 PASS 不會清除。測試完成後保持 USB 供電且不要
按 RESET，再連接 GUI。Log tab 會列出 `OFFLINE_FAILURE_HISTORY_SUMMARY` 與每一筆
`OFFLINE_FAILURE_HISTORY`。容量滿時 summary 會顯示 `overflow=1` 與 dropped count。
History-enabled build 的實體讀回與穩定度複測仍待執行，因此目前仍不可宣告
offline programming 穩定完成。

`DSLogic PLus-la-260722-123827.csv`／`decoder--260722-123827.csv` 與
`Rl78ProgrammerTool_20260722_123551_628_16236.log` 已驗證 history correlation：
38 次 job 中 25 次完成，13 次在 program data loop 中止；history 的 job counter
8、19、21-24、27-29、31、36-38 正好對上 LA 的 13 次截斷 transaction。13 筆皆為
`TARGET_LINK_DIRECTION_COLLISION`，失敗 snapshot 都顯示 PA.9 Low、PA.10 High。
這證明診斷可用。後續 reset-only build 為 9/28 完成，19 次失敗全是相同 collision，
且 19 次都在約 1 秒後出現預期 link reset；這也證明單次 reset 無法修復已部分寫入的
G15。Bridge／target 完整重新上下電後，`FW 1.0.1.4` 已完成 64/64 次 G15
8 KiB KEY1 transaction；每次都有正確 P21 payload、2,048 個 data ACK 與 final
verify ACK，但測試未觸發 collision retry。`FW 1.0.1.5` 已將 GUI `RESET MCU`
從 CPU-only reset 改成 M484 full-chip reset，以清除 GPIO、TIMER、SPIM、HSUSBD
內部狀態，再由 resident bootloader 驗證並啟動 application。外部 W25Q16、G15
與其供電不會被 power-cycle；更新此版本後，仍須實測 reset/re-enumeration、無重新
上下電的後續 KEY1 programming，以及強制 collision retry。

兩分鐘 stress test 開始前，可先 reset bridge 一次清空舊 history；等待 USB 重新
enumeration 後保持 USB 供電、關閉或斷開 GUI，再開始 LA capture 並重複按 KEY1。
Capture 結束後不要再次 reset，直接連接 GUI。只有 summary 顯示 `dropped=0` 時，
該批 LA 才能確認沒有因 ring overflow 遺失 failure record。
