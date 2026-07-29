param(
    [string]$UVision = "C:\Keil_v5\UV4\UV4.exe",
    [string]$Target = "Release",
    [string]$SRecord = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sampleRoot = Join-Path $root "demo_code\M032BSP_RL78_Programmer\SampleCode"
$bootDirectory = Join-Path $sampleRoot "ISP_UART\KEIL"
$bootProject = Join-Path $bootDirectory "ISP_UART.uvprojx"
$bootDriverConfig = Join-Path $bootDirectory "Nu_Link_Driver.ini"
$appDirectory = Join-Path $sampleRoot "RL78_Programmer\KEIL"
$appProject = Join-Path $appDirectory "M032_TEST_BOARD_APP.uvprojx"
$bootObj = Join-Path $bootDirectory "obj"
$bootLst = Join-Path $bootDirectory "lst"
$appObj = Join-Path $appDirectory "obj"
$appLst = Join-Path $appDirectory "lst"
$bootBuildLog = Join-Path $bootObj "M032TestBoardUartBootloader.build.log"
$appBuildLog = Join-Path $appObj "M032TestBoardBridge.build.log"
$bootMain = Join-Path $sampleRoot "ISP_UART\main.c"
$testBoardMain = Join-Path $root "firmware\m032_test_board\m032_test_main.c"
$testBoardTool0 = Join-Path $root "firmware\m032_test_board\m032_test_tool0_soft_uart.c"
$testBoardUart = Join-Path $root "firmware\m032_test_board\m032_test_uart_port.c"
$numicroStorage = Join-Path $root "firmware\storage\m032_numicro_fmc_storage.c"
$protocolB = Join-Path $root "firmware\target\rl78_protocol_b.c"

$bootLimit = 0x4000
$appStart = 0x4000
$appEnd = 0xFC00
$appSize = $appEnd - $appStart
$fmcPageSize = 0x200
$crcPageStart = $appEnd - $fmcPageSize
$crcAddress = $appEnd - 4
$linkableAppLimit = $crcPageStart - $appStart
$factorySize = $appEnd

function Resolve-SRecord {
    param([string]$Requested)

    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        if (-not (Test-Path -LiteralPath $Requested)) {
            throw "SRecord executable was not found: $Requested"
        }
        return (Resolve-Path -LiteralPath $Requested).Path
    }

    $command = Get-Command "srec_cat.exe" -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        $command = Get-Command "srec_cat" -ErrorAction SilentlyContinue
    }
    if ($null -eq $command) {
        $knownInstall = "C:\Program Files (x86)\Texas Instruments\Fusion Digital Power Designer\bin\srec_cat.exe"
        if (Test-Path -LiteralPath $knownInstall -PathType Leaf) {
            return (Resolve-Path -LiteralPath $knownInstall).Path
        }
        throw "srec_cat.exe was not found. Install SRecord, add it to PATH, or pass -SRecord."
    }
    return $command.Source
}

function Invoke-KeilProject {
    param(
        [string]$Directory,
        [string]$ProjectName,
        [string]$TargetName,
        [string]$LogPath
    )

    New-Item -ItemType Directory -Force -Path (Join-Path $Directory "obj"), (Join-Path $Directory "lst") | Out-Null
    Remove-Item -LiteralPath $LogPath -ErrorAction SilentlyContinue
    $arguments = "-b $ProjectName -t $TargetName -j0 -o `"$LogPath`""
    $process = Start-Process -FilePath $UVision -ArgumentList $arguments -WorkingDirectory $Directory -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        if (Test-Path -LiteralPath $LogPath) {
            Get-Content -LiteralPath $LogPath
        }
        throw "Keil uVision build failed for $ProjectName with exit code $($process.ExitCode)"
    }
    if (-not (Test-Path -LiteralPath $LogPath)) {
        throw "Keil build log was not created: $LogPath"
    }
    $logText = Get-Content -Raw -LiteralPath $LogPath
    if ($logText -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw "Keil build did not report a clean result: $LogPath"
    }
}

function Get-LoadRegionBytes {
    param([string]$MapPath)

    $match = Select-String -Path $MapPath -Pattern 'Load Region \S+ .*Size: 0x([0-9a-fA-F]+)' | Select-Object -First 1
    if ($null -eq $match) {
        throw "Load-region size was not found in $MapPath"
    }
    return [Convert]::ToUInt32($match.Matches[0].Groups[1].Value, 16)
}

function Assert-ExecutionBase {
    param(
        [string]$MapPath,
        [uint32]$ExpectedBase
    )

    $expected = ('Exec base: 0x{0:x8}' -f $ExpectedBase)
    if (-not (Select-String -Path $MapPath -SimpleMatch $expected -Quiet)) {
        throw "Expected execution base $expected was not found in $MapPath"
    }
}

function Get-Crc32IsoHdlc {
    param(
        [byte[]]$Bytes,
        [int]$Count
    )

    [uint32]$crc = [uint32]::MaxValue
    [uint32]$polynomial = [Convert]::ToUInt32("EDB88320", 16)
    for ($index = 0; $index -lt $Count; ++$index) {
        $crc = [uint32]($crc -bxor [uint32]$Bytes[$index])
        for ($bit = 0; $bit -lt 8; ++$bit) {
            if (($crc -band 1) -ne 0) {
                $crc = [uint32](($crc -shr 1) -bxor $polynomial)
            } else {
                $crc = [uint32]($crc -shr 1)
            }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

foreach ($required in @($UVision, $bootProject, $bootDriverConfig, $bootMain, $appProject, $testBoardMain, $testBoardTool0, $testBoardUart, $numicroStorage, $protocolB)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required Keil build input was not found: $required"
    }
}

$bootMainText = Get-Content -Raw -LiteralPath $bootMain
if (($bootMainText -notmatch 'static void Iap_ResetToApplication') -or
    ($bootMainText -notmatch 'SYS_IPRST0_CHIPRST_Msk')) {
    throw "M032 TEST BOARD CMD_RUN_APROM must use a clean full-chip reset before application launch."
}

$testBoardMainText = Get-Content -Raw -LiteralPath $testBoardMain
if ($testBoardMainText -match '\bCLK_SysTickDelay\s*\(') {
    throw "M032 TEST BOARD DelayUs must not reconfigure or disable the SysTick timeout clock."
}
if (($testBoardMainText -notmatch 'CLK_EnableModuleClock\s*\(\s*TMR1_MODULE\s*\)') -or
    ($testBoardMainText -notmatch 'TIMER1->CTL\s*=\s*TIMER_ONESHOT_MODE')) {
    throw "M032 TEST BOARD must use the dedicated TIMER1 microsecond delay clock."
}
$indicatorFunction = [regex]::Match(
    $testBoardMainText,
    '(?ms)^static void UpdateIndicator\(.*?^}')
if (-not $indicatorFunction.Success -or
    $indicatorFunction.Value -notmatch
        '(?s)PLP_OPERATION_FAILED.*?\{\s*leds\s*=\s*M032_TEST_LED_RED;\s*\}\s*else if' -or
    $indicatorFunction.Value -notmatch 'PLP_OPERATION_CANCELLED' -or
    $indicatorFunction.Value -notmatch 'PLP_TERMINAL_CANCELLED' -or
    $indicatorFunction.Value -notmatch
        '(?s)PLP_OPERATION_RUNNING.*?now_ms\s*/\s*125u.*?leds\s*=\s*M032_TEST_LED_RED' -or
    $indicatorFunction.Value -notmatch
        '(?s)PLP_TERMINAL_PASS.*?leds\s*=\s*M032_TEST_LED_GREEN') {
    throw "M032 TEST BOARD must indicate programming with blinking Red, terminal failure with solid Red and terminal pass with solid Green."
}

$testBoardTool0Text = Get-Content -Raw -LiteralPath $testBoardTool0
if (($testBoardTool0Text -notmatch 'SYS_GPB_MFPH_PB9MFP_GPIO') -or
    ($testBoardTool0Text -notmatch 'SYS_GPB_MFPH_PB10MFP_GPIO') -or
    ($testBoardTool0Text -notmatch 'GPIO_EnableInt\s*\(\s*PB\s*,\s*10u') -or
    ($testBoardTool0Text -notmatch 'void GPABGH_IRQHandler') -or
    ($testBoardTool0Text -notmatch 'static void StartReceiveFrame') -or
    ($testBoardTool0Text -notmatch 'ProcessReceiveBitTimer') -or
    ($testBoardTool0Text -match '\bUART4\b')) {
    throw "M032 TEST BOARD must use PB9 TIMER2/GPIO software TX and PB10 GPIO/TIMER2 software RX without UART4."
}
if (($testBoardTool0Text -match 'TOOL0_TIMER_STATE_RX_START') -or
    ($testBoardTool0Text -match 'TOOL0_TIMER_HALF_BIT_TICKS')) {
    throw "M032 TEST BOARD RX must use one periodic schedule from the PB10 falling edge; the two-stage RX timer adds excessive Cortex-M0 latency."
}
if (($testBoardTool0Text -notmatch 'static void StartPollingTimer') -or
    ($testBoardTool0Text -notmatch 'while\s*\(\s*uart->tx_bit_index\s*<\s*TOOL0_TX_FIRST_STOP_BIT\s*\)')) {
    throw "M032 TEST BOARD TX start/data/parity bits must use bounded TIMER2 polling so host UART activity cannot drop a TOOL0 bit."
}
if ($testBoardTool0Text -match 'GPIO_EnableInt\s*\(\s*PB\s*,\s*9') {
    throw "M032 TEST BOARD PB9 is TOOL0 transmit; it must not be configured as the receive edge input."
}

$testBoardUartText = Get-Content -Raw -LiteralPath $testBoardUart
if (($testBoardUartText -notmatch 'NVIC_DisableIRQ\s*\(\s*UART02_IRQn\s*\)') -or
    ($testBoardUartText -notmatch 'NVIC_EnableIRQ\s*\(\s*UART02_IRQn\s*\)') -or
    ($testBoardUartText -match '\b__disable_irq\s*\(') -or
    ($testBoardUartText -match '\b__set_PRIMASK\s*\(')) {
    throw "M032 TEST BOARD PC UART critical sections must mask only UART02_IRQn so TOOL0 TIMER2 cannot lose a bit period."
}

$numicroStorageText = Get-Content -Raw -LiteralPath $numicroStorage
$numicroStorageExecutable = [regex]::Replace(
    $numicroStorageText,
    '(?s)/\*.*?\*/',
    '')
$fmcReadFunction = [regex]::Match(
    $numicroStorageText,
    '(?ms)^static uint32_t FmcRead\(.*?^}')
$fmcReadExecutable = [regex]::Replace(
    $fmcReadFunction.Value,
    '(?s)/\*.*?\*/',
    '')
if (-not $fmcReadFunction.Success -or
    $fmcReadExecutable -notmatch '\bM8\s*\(\s*address\s*\)' -or
    $numicroStorageExecutable -match '\bFMC_Read\s*\(') {
    throw "M032 APROM package reads must use memory-mapped access; protected FMC_Read triggers can return stale data after SYS_LockReg."
}

$protocolBText = Get-Content -Raw -LiteralPath $protocolB
if (($protocolBText -notmatch 'static uint32_t CalculateDenseImageCrc') -or
    ($protocolBText -notmatch 'transmitted_image_crc32') -or
    ($protocolBText -notmatch 'PLP_ERR_INTEGRITY_CRC')) {
    throw "Protocol B must compare the pre-entry dense-image CRC with the bytes supplied to TOOL0 programming."
}

$bootDriverText = Get-Content -Raw -LiteralPath $bootDriverConfig
$m031Driver = [regex]::Match(
    $bootDriverText,
    '(?ms)^\[M031\]\s*(?<body>.*?)(?=^\[|\z)')
if (-not $m031Driver.Success -or
    $m031Driver.Groups['body'].Value -notmatch '(?m)^FlashSelect=APROM\s*$' -or
    $m031Driver.Groups['body'].Value -notmatch '(?m)^ProgramAlgorithm=M031_AP_128\.FLM\s*$') {
    throw "M032 TEST BOARD UART bootloader Nu-Link configuration must target APROM with M031_AP_128.FLM."
}

$srecordPath = Resolve-SRecord -Requested $SRecord
if ((Resolve-Path -LiteralPath $srecordPath).Path.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "SRecord must be installed outside the workspace; do not commit srec_cat.exe."
}

$appProjectText = Get-Content -Raw -LiteralPath $appProject
if ($appProjectText -match 'PAGE_SIZE_2048') {
    throw "M032SE IAP requires the 0x200-byte FMC page; PAGE_SIZE_2048 must not be defined."
}
$scatterPath = Join-Path $appDirectory "M032_APROM_64K_SLOT.sct"
$scatterText = Get-Content -Raw -LiteralPath $scatterPath
if (($scatterText -notmatch '#define FLASH_BASE\s+0x00004000') -or
    ($scatterText -notmatch '#define APPLICATION_SIZE\s+0x0000BC00') -or
    ($scatterText -notmatch '#define CRC_PAGE_SIZE\s+0x00000200') -or
    ($scatterText -notmatch '#define STACK_SIZE\s+0x00001000')) {
    throw "M032 application scatter layout does not match the approved IAP partition."
}

& (Join-Path $PSScriptRoot "sync_m032_sample.ps1")

$savedSRecord = $env:SREC_CAT_EXE
try {
    $env:SREC_CAT_EXE = $srecordPath
    Invoke-KeilProject -Directory $bootDirectory -ProjectName "ISP_UART.uvprojx" -TargetName "ISP_UART" -LogPath $bootBuildLog
    Invoke-KeilProject -Directory $appDirectory -ProjectName "M032_TEST_BOARD_APP.uvprojx" -TargetName $Target -LogPath $appBuildLog
} finally {
    $env:SREC_CAT_EXE = $savedSRecord
}

$bootImage = Join-Path $bootObj "ISP_UART.axf"
$bootBinary = Join-Path $bootObj "ISP_UART.bin"
$bootHex = Join-Path $bootObj "ISP_UART.hex"
$bootMap = Join-Path $bootLst "ISP_UART.map"
$appImage = Join-Path $appObj "M032TestBoardBridge.axf"
$appBinary = Join-Path $appObj "M032TestBoardBridge.bin"
$appHex = Join-Path $appObj "M032TestBoardBridge.hex"
$appMap = Join-Path $appLst "M032TestBoardBridge.map"
$factoryBinary = Join-Path $appObj "M032TestBoardBridge_factory.bin"
$factoryHex = Join-Path $appObj "M032TestBoardBridge_factory.hex"

$bootRomBytes = Get-LoadRegionBytes -MapPath $bootMap
$appRomBytes = Get-LoadRegionBytes -MapPath $appMap
if ($bootRomBytes -gt $bootLimit) {
    throw "M032 bootloader exceeds 16 KiB: $bootRomBytes bytes"
}
if ((Get-Item -LiteralPath $bootBinary).Length -gt $bootLimit) {
    throw "M032 bootloader BIN crosses 0x3FFF."
}
if ($appRomBytes -gt $linkableAppLimit) {
    throw "M032 linked application reaches the reserved CRC erase page: $appRomBytes bytes"
}
Assert-ExecutionBase -MapPath $bootMap -ExpectedBase 0x00000000
Assert-ExecutionBase -MapPath $appMap -ExpectedBase $appStart

$appBytes = [System.IO.File]::ReadAllBytes($appBinary)
if ($appBytes.Length -ne $appSize) {
    throw "M032 application BIN must be exactly 0xBC00 bytes; actual=$($appBytes.Length)."
}
for ($index = $linkableAppLimit; $index -lt ($appSize - 4); ++$index) {
    if ($appBytes[$index] -ne 0xFF) {
        throw ('Reserved CRC erase page contains linked data at application offset 0x{0:X}.' -f $index)
    }
}
$storedCrc = [BitConverter]::ToUInt32($appBytes, $appSize - 4)
$calculatedCrc = Get-Crc32IsoHdlc -Bytes $appBytes -Count ($appSize - 4)
if ($storedCrc -ne $calculatedCrc) {
    throw ('M032 application CRC mismatch: stored=0x{0:X8}, calculated=0x{1:X8}.' -f $storedCrc, $calculatedCrc)
}
$stackWord = [BitConverter]::ToUInt32($appBytes, 0)
$resetWord = [BitConverter]::ToUInt32($appBytes, 4)
$resetAddress = $resetWord -band 0xFFFFFFFE
if (($stackWord -lt 0x20000000) -or ($stackWord -gt 0x20004000) -or (($stackWord -band 3) -ne 0)) {
    throw ('M032 application vector has an invalid initial stack pointer: 0x{0:X8}.' -f $stackWord)
}
if ((($resetWord -band 1) -eq 0) -or ($resetAddress -lt $appStart) -or ($resetAddress -ge $crcPageStart)) {
    throw ('M032 application vector has an invalid reset handler: 0x{0:X8}.' -f $resetWord)
}

$factoryBytes = [System.IO.File]::ReadAllBytes($factoryBinary)
if ($factoryBytes.Length -ne $factorySize) {
    throw "M032 factory BIN must end at 0xFBFF; actual size=$($factoryBytes.Length)."
}
for ($index = 0; $index -lt $appSize; ++$index) {
    if ($factoryBytes[$appStart + $index] -ne $appBytes[$index]) {
        throw ('Factory/application mismatch at APROM address 0x{0:X8}.' -f ($appStart + $index))
    }
}

$bootFree = $bootLimit - $bootRomBytes
$appFree = $linkableAppLimit - $appRomBytes
Write-Output ("M032 TEST BOARD Keil {0} passed: UART boot {1:N2} KiB ({2:N2} KiB free), app {3:N2} KiB ({4:N2} KiB linkable free)." -f
    $Target, ($bootRomBytes / 1024.0), ($bootFree / 1024.0), ($appRomBytes / 1024.0), ($appFree / 1024.0))
Write-Output ('Application image: 0x{0:X} bytes, CRC32=0x{1:X8}, CRC word at APROM 0x{2:X8}.' -f $appSize, $storedCrc, $crcAddress)
Write-Output ('Factory image: 0x{0:X} bytes, ends before metadata at APROM 0x{1:X8}.' -f $factorySize, $appEnd)
Write-Output "Bootloader HEX: $bootHex"
Write-Output "Application BIN: $appBinary"
Write-Output "Factory HEX: $factoryHex"
Write-Output "Boot build log: $bootBuildLog"
Write-Output "App build log: $appBuildLog"
