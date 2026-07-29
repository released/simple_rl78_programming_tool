# NuMaker-M032SE 與 RL78 接線說明

本接線適用於 M032SE3AE RL78 bridge firmware。M032 的 TOOL0 實作採用 PA.3
single-wire half-duplex UART，因此只需要一個 MCU pin 連至 RL78 `TOOL0`；PA.2
並不是第二條必要的 receive line。

## 接線表

| 功能 | M032SE3AE pin | EVB / Arduino | RL78 target | 接法 |
| --- | --- | --- | --- | --- |
| TOOL0 single-wire | PA.3 | JP5.21/JP5.22 / D10 | `TOOL0` | 經 target-domain interface 連接；3.3 V 同電壓 domain 可使用 `1 kΩ` 串聯電阻 |
| Reserved UART RX | PA.2 | JP5.23/JP5.24 / D13 | - | `NC`；single-wire mode 不使用此 pin |
| Target reset | PA.5 | JP5.17/JP5.18 / D8 | `RESET#` | 使用 open-drain interface；pull-up 位於 target VDD domain |
| Reserved VDD sense | PB.3 | JP3.7/JP3.8 / D1 | - | `NC`；firmware 不執行 VDD sense |
| Reserved power enable | PA.4 | JP5.19/JP5.20 / D9 | - | 目前 profile 為 `NC` |
| Upgrade status LED | PB.14 | On-board LED | - | Active-low on-board indicator |
| Common reference | GND | EVB GND | `GND` | 必須共地 |

PB.12、PB.13 是 EVB VCOM UART0 使用的 pins，不作為 RL78 programming signal。

```text
M032 PA.3 / D10 ----[target-domain interface]---- RL78 TOOL0
M032 PA.5 / D8  ----[open-drain interface]------- RL78 RESET#
M032 GND ---------------------------------------- RL78 GND

M032 PA.2 / D13 --------------------------------- NC
M032 PA.4 / D9  --------------------------------- NC
M032 PB.3 / D1  --------------------------------- NC
```

## 為何 M032 只使用 PA.3

M032 firmware 讓 PA.3 在 transmit 與 receive 之間切換方向，形成 single-wire
half-duplex bus。Nu-Link2-Me 則使用獨立的 PA.9 TX 與 PA.10 RX，再以兩個串聯電阻
於 target-side 合併；兩種硬體 topology 不同，但對 RL78 target 都只呈現一條
`TOOL0` signal。因此 M032 不需要仿照 Nu-Link2-Me 再使用 PA.2。

若 RL78 target 也是 3.3 V domain，PA.3 可經建議的 `1 kΩ` 串聯電阻連至 TOOL0。
若 target VDD 不是 3.3 V，PA.3 仍必須經由以 target VDD 為參考的 bidirectional
level interface；不可將 5 V TOOL0 直接接到 M032 pin。PA.5 的 RESET interface 也
必須維持 open-drain，並由 target board 自己的 VDD domain pull-up。

PB.3 保持 `NC`，目前 firmware 已取消 target VDD sense 與強制 voltage interlock；
這不代表可以省略正確的 level interface。

M032 EVB 的 offline trigger 是 external Reset button。Reset 會重新啟動 bridge，
經 HID grace period 後開始新的 handshake；若 target 尚未回應，firmware 仍會在
安全釋放 pins 後每 500 ms 自動重試。共用 programmer core 也只允許在 erase 前
重啟 handshake，進入 erase/program 後不會中斷 destructive operation。

## Bootloader 與 application firmware

Bridge firmware 採 resident bootloader 加 application 的配置：

- Bootloader 位於 APROM `0x00000000-0x00003FFF`，正常的 GUI firmware update
  不會覆寫此區。
- Application 從 APROM `0x00004000` 開始。
- 首次以 Nu-Link/SWD 建立整板映像時，可使用
  `SampleCode/RL78_Programmer/KEIL/obj/M032Rl78Bridge_factory.hex`。
- 單獨燒錄 resident bootloader 時，使用
  `SampleCode/ISP_HID/KEIL/obj/ISP_HID.hex`。
- GUI 的 `FW Upload (USB HID)` 使用固定長度且已附 CRC32 的 application image：
  `SampleCode/RL78_Programmer/KEIL/obj/M032Rl78Bridge.bin`。

Application 執行時可由 GUI 執行 `Return Boot loader`；firmware 會抹除 application
CRC validity marker，reset 後 resident bootloader 會留在 USB HID ISP mode 等待重新
上傳 application。更新成功後再由 GUI 執行 `Run Application`。若更新途中斷電，
bootloader 仍保留，重新連線並再次上傳 application 即可。

## 驗證狀態

實體 USB HID enumeration、TOOL0/RESET waveform、RL78 erase/program/verify，以及
reset button 觸發的 offline programming HIL 均為 **NOT RUN**。接上實際 target 前
仍需以限流電源及示波器完成驗證。
