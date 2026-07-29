param(
    [string]$UVision = "C:\Keil_v5\UV4\UV4.exe",
    [string]$Target = "Release",
    [string]$SRecord = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sampleRoot = Join-Path $root "demo_code\M032BSP_RL78_Programmer\SampleCode"
$bootDirectory = Join-Path $sampleRoot "ISP_HID\KEIL"
$bootProject = Join-Path $bootDirectory "ISP_HID.uvprojx"
$bootDriverConfig = Join-Path $bootDirectory "Nu_Link_Driver.ini"
$appDirectory = Join-Path $sampleRoot "RL78_Programmer\KEIL"
$appProject = Join-Path $appDirectory "Template.uvprojx"
$bootObj = Join-Path $bootDirectory "obj"
$bootLst = Join-Path $bootDirectory "lst"
$appObj = Join-Path $appDirectory "obj"
$appLst = Join-Path $appDirectory "lst"
$bootBuildLog = Join-Path $bootObj "M032Bootloader.build.log"
$appBuildLog = Join-Path $appObj "M032Rl78Bridge.build.log"

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

foreach ($required in @($UVision, $bootProject, $bootDriverConfig, $appProject)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required Keil build input was not found: $required"
    }
}

$bootDriverText = Get-Content -Raw -LiteralPath $bootDriverConfig
$m031Driver = [regex]::Match(
    $bootDriverText,
    '(?ms)^\[M031\]\s*(?<body>.*?)(?=^\[|\z)')
if (-not $m031Driver.Success -or
    $m031Driver.Groups['body'].Value -notmatch '(?m)^FlashSelect=APROM\s*$' -or
    $m031Driver.Groups['body'].Value -notmatch '(?m)^ProgramAlgorithm=M031_AP_128\.FLM\s*$') {
    throw "M032 resident bootloader Nu-Link download configuration must target APROM with M031_AP_128.FLM."
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
    ($scatterText -notmatch '#define CRC_PAGE_SIZE\s+0x00000200')) {
    throw "M032 application scatter layout does not match the approved IAP partition."
}

& (Join-Path $PSScriptRoot "sync_m032_sample.ps1")

$savedSRecord = $env:SREC_CAT_EXE
try {
    $env:SREC_CAT_EXE = $srecordPath
    Invoke-KeilProject -Directory $bootDirectory -ProjectName "ISP_HID.uvprojx" -TargetName "ISP_HID" -LogPath $bootBuildLog
    Invoke-KeilProject -Directory $appDirectory -ProjectName "Template.uvprojx" -TargetName $Target -LogPath $appBuildLog
} finally {
    $env:SREC_CAT_EXE = $savedSRecord
}

$bootImage = Join-Path $bootObj "ISP_HID.axf"
$bootBinary = Join-Path $bootObj "ISP_HID.bin"
$bootHex = Join-Path $bootObj "ISP_HID.hex"
$bootMap = Join-Path $bootLst "ISP_HID.map"
$appImage = Join-Path $appObj "M032Rl78Bridge.axf"
$appBinary = Join-Path $appObj "M032Rl78Bridge.bin"
$appHex = Join-Path $appObj "M032Rl78Bridge.hex"
$appMap = Join-Path $appLst "M032Rl78Bridge.map"
$factoryBinary = Join-Path $appObj "M032Rl78Bridge_factory.bin"
$factoryHex = Join-Path $appObj "M032Rl78Bridge_factory.hex"

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
Write-Output ("M032 Keil {0} passed: boot {1:N2} KiB ({2:N2} KiB free), app {3:N2} KiB ({4:N2} KiB linkable free)." -f
    $Target, ($bootRomBytes / 1024.0), ($bootFree / 1024.0), ($appRomBytes / 1024.0), ($appFree / 1024.0))
Write-Output ('Application image: 0x{0:X} bytes, CRC32=0x{1:X8}, CRC word at APROM 0x{2:X8}.' -f $appSize, $storedCrc, $crcAddress)
Write-Output ('Factory image: 0x{0:X} bytes, ends before metadata at APROM 0x{1:X8}.' -f $factorySize, $appEnd)
Write-Output "Bootloader HEX: $bootHex"
Write-Output "Application BIN: $appBinary"
Write-Output "Factory HEX: $factoryHex"
Write-Output "Boot build log: $bootBuildLog"
Write-Output "App build log: $appBuildLog"
