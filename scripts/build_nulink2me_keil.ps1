param(
    [string]$UVision = "C:\Keil_v5\UV4\UV4.exe",
    [string]$Target = "Release",
    [string]$SRecord = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sampleRoot = Join-Path $root "demo_code\M480BSP_Nulink2Me_RL78\SampleCode"
$bootDirectory = Join-Path $sampleRoot "ISP_HID_20\KEIL"
$bootProject = Join-Path $bootDirectory "ISP_HID_20.uvprojx"
$bootDriverConfig = Join-Path $bootDirectory "Nu_Link_Driver.ini"
$appDirectory = Join-Path $sampleRoot "Template\Keil"
$appProject = Join-Path $appDirectory "Template.uvprojx"
$bootObj = Join-Path $bootDirectory "obj"
$bootLst = Join-Path $bootDirectory "lst"
$appObj = Join-Path $appDirectory "obj"
$appLst = Join-Path $appDirectory "lst"
$bootBuildLog = Join-Path $bootObj "NuLink2MeIapBoot.build.log"
$appBuildLog = Join-Path $appObj "NuLink2MeRl78Bridge.build.log"

$bootLimit = 0x4000
$appStart = 0x4000
$appEnd = 0x80000
$appSize = $appEnd - $appStart
$fmcPageSize = 0x1000
$crcPageStart = $appEnd - $fmcPageSize
$crcAddress = $appEnd - 4
$linkableAppLimit = $crcPageStart - $appStart
$factorySize = $appEnd

function Resolve-SRecord {
    param([string]$Requested)

    $candidate = $null
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        if (-not (Test-Path -LiteralPath $Requested -PathType Leaf)) {
            throw "SRecord executable was not found: $Requested"
        }
        $candidate = (Resolve-Path -LiteralPath $Requested).Path
    } else {
        $command = Get-Command "srec_cat.exe" -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            $candidate = $command.Source
        } else {
            $knownInstall = "C:\Program Files (x86)\Texas Instruments\Fusion Digital Power Designer\bin\srec_cat.exe"
            if (Test-Path -LiteralPath $knownInstall -PathType Leaf) {
                $candidate = (Resolve-Path -LiteralPath $knownInstall).Path
            }
        }
    }

    if ($null -eq $candidate) {
        throw "srec_cat.exe was not found. Install SRecord, add it to PATH, or pass -SRecord."
    }
    if ([System.IO.Path]::GetFileName($candidate) -ine "srec_cat.exe") {
        throw "The Nu-Link2-Me post-build step requires an executable named srec_cat.exe: $candidate"
    }

    $resolvedRoot = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    $resolvedCandidate = [System.IO.Path]::GetFullPath($candidate)
    if ($resolvedCandidate.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "SRecord must be installed outside the workspace; do not commit srec_cat.exe."
    }
    return $resolvedCandidate
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
    $process = Start-Process -FilePath $UVision -ArgumentList $arguments `
        -WorkingDirectory $Directory -Wait -PassThru -WindowStyle Hidden
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
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required Keil build input was not found: $required"
    }
}

$bootDriverText = Get-Content -Raw -LiteralPath $bootDriverConfig
$m481Driver = [regex]::Match(
    $bootDriverText,
    '(?ms)^\[M481\]\s*(?<body>.*?)(?=^\[|\z)')
if (-not $m481Driver.Success -or
    $m481Driver.Groups['body'].Value -notmatch '(?m)^FlashSelect=APROM\s*$' -or
    $m481Driver.Groups['body'].Value -notmatch '(?m)^ProgramAlgorithm=M481_AP_512\.FLM\s*$') {
    throw "M484 resident bootloader Nu-Link download configuration must target APROM with M481_AP_512.FLM."
}

$srecordPath = Resolve-SRecord -Requested $SRecord
$scatterPath = Join-Path $appDirectory "M484_APROM.sct"
if (-not (Test-Path -LiteralPath $scatterPath -PathType Leaf)) {
    throw "M484 application scatter file was not found: $scatterPath"
}
$scatterText = Get-Content -Raw -LiteralPath $scatterPath
if (($scatterText -notmatch '#define FLASH_BASE\s+0x00004000') -or
    ($scatterText -notmatch '#define FLASH_SIZE\s+0x0007B000')) {
    throw "M484 application scatter layout does not reserve the approved final 4 KiB CRC page."
}

& (Join-Path $PSScriptRoot "sync_nulink2me_sample.ps1")
& (Join-Path $PSScriptRoot "sync_nulink2me_sample.ps1") -Check

$factoryBinary = Join-Path $appObj "NuLink2Me_boot_app.bin"
$factoryHex = Join-Path $appObj "NuLink2Me_boot_app.hex"
$derivedArtifacts = @(
    (Join-Path $appObj "NuLink2MeRl78Bridge.bin"),
    (Join-Path $appObj "NuLink2MeRl78Bridge_CRC.hex"),
    $factoryBinary,
    $factoryHex
)
foreach ($artifact in $derivedArtifacts) {
    Remove-Item -LiteralPath $artifact -ErrorAction SilentlyContinue
}

$savedPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $srecordPath) + ";" + $savedPath
    Invoke-KeilProject -Directory $bootDirectory -ProjectName "ISP_HID_20.uvprojx" `
        -TargetName "ISP_HID_20" -LogPath $bootBuildLog
    Invoke-KeilProject -Directory $appDirectory -ProjectName "Template.uvprojx" `
        -TargetName $Target -LogPath $appBuildLog
} finally {
    $env:PATH = $savedPath
}

$bootImage = Join-Path $bootObj "NuLink2MeIapBoot.axf"
$bootBinary = Join-Path $bootObj "NuLink2MeIapBoot.bin"
$bootHex = Join-Path $bootObj "NuLink2MeIapBoot.hex"
$bootMap = Join-Path $bootLst "NuLink2MeIapBoot.map"
$appImage = Join-Path $appObj "NuLink2MeRl78Bridge.axf"
$appBinary = Join-Path $appObj "NuLink2MeRl78Bridge.bin"
$appHex = Join-Path $appObj "NuLink2MeRl78Bridge.hex"
$appMap = Join-Path $appLst "NuLink2MeRl78Bridge.map"

$bootRomBytes = Get-LoadRegionBytes -MapPath $bootMap
$appRomBytes = Get-LoadRegionBytes -MapPath $appMap
if ($bootRomBytes -gt $bootLimit) {
    throw "Nu-Link2-Me bootloader exceeds 16 KiB: $bootRomBytes bytes"
}
if ((Get-Item -LiteralPath $bootBinary).Length -gt $bootLimit) {
    throw "Nu-Link2-Me bootloader BIN crosses 0x3FFF."
}
if ($appRomBytes -gt $linkableAppLimit) {
    throw "Nu-Link2-Me linked application reaches the reserved CRC erase page: $appRomBytes bytes"
}
Assert-ExecutionBase -MapPath $bootMap -ExpectedBase 0x00000000
Assert-ExecutionBase -MapPath $appMap -ExpectedBase $appStart

$appBytes = [System.IO.File]::ReadAllBytes($appBinary)
if ($appBytes.Length -ne $appSize) {
    throw ('Nu-Link2-Me application BIN must be exactly 0x{0:X} bytes; actual=0x{1:X}.' -f $appSize, $appBytes.Length)
}
for ($index = $linkableAppLimit; $index -lt ($appSize - 4); ++$index) {
    if ($appBytes[$index] -ne 0xFF) {
        throw ('Reserved CRC erase page contains linked data at application offset 0x{0:X}.' -f $index)
    }
}
$storedCrc = [BitConverter]::ToUInt32($appBytes, $appSize - 4)
$calculatedCrc = Get-Crc32IsoHdlc -Bytes $appBytes -Count ($appSize - 4)
if ($storedCrc -ne $calculatedCrc) {
    throw ('Nu-Link2-Me application CRC mismatch: stored=0x{0:X8}, calculated=0x{1:X8}.' -f $storedCrc, $calculatedCrc)
}
$stackWord = [BitConverter]::ToUInt32($appBytes, 0)
$resetWord = [BitConverter]::ToUInt32($appBytes, 4)
$resetAddress = $resetWord -band 0xFFFFFFFE
if (($stackWord -lt 0x20000000) -or ($stackWord -gt 0x20028000) -or (($stackWord -band 7) -ne 0)) {
    throw ('Nu-Link2-Me application vector has an invalid initial stack pointer: 0x{0:X8}.' -f $stackWord)
}
if ((($resetWord -band 1) -eq 0) -or ($resetAddress -lt $appStart) -or ($resetAddress -ge $crcPageStart)) {
    throw ('Nu-Link2-Me application vector has an invalid reset handler: 0x{0:X8}.' -f $resetWord)
}

$factoryBytes = [System.IO.File]::ReadAllBytes($factoryBinary)
if ($factoryBytes.Length -ne $factorySize) {
    throw ('Nu-Link2-Me factory BIN must be exactly 0x{0:X} bytes; actual=0x{1:X}.' -f $factorySize, $factoryBytes.Length)
}
$bootBytes = [System.IO.File]::ReadAllBytes($bootBinary)
for ($index = 0; $index -lt $bootBytes.Length; ++$index) {
    if ($factoryBytes[$index] -ne $bootBytes[$index]) {
        throw ('Factory/bootloader mismatch at APROM address 0x{0:X8}.' -f $index)
    }
}
for ($index = $bootBytes.Length; $index -lt $appStart; ++$index) {
    if ($factoryBytes[$index] -ne 0xFF) {
        throw ('Factory image bootloader padding is not erased at APROM address 0x{0:X8}.' -f $index)
    }
}
for ($index = 0; $index -lt $appSize; ++$index) {
    if ($factoryBytes[$appStart + $index] -ne $appBytes[$index]) {
        throw ('Factory/application mismatch at APROM address 0x{0:X8}.' -f ($appStart + $index))
    }
}

$bootFree = $bootLimit - $bootRomBytes
$appFree = $linkableAppLimit - $appRomBytes
Write-Output ("Nu-Link2-Me Keil {0} passed: boot {1:N2} KiB ({2:N2} KiB free), app {3:N2} KiB ({4:N2} KiB linkable free)." -f
    $Target, ($bootRomBytes / 1024.0), ($bootFree / 1024.0), ($appRomBytes / 1024.0), ($appFree / 1024.0))
Write-Output ('Application image: 0x{0:X} bytes, CRC32=0x{1:X8}, CRC word at APROM 0x{2:X8}.' -f $appSize, $storedCrc, $crcAddress)
Write-Output ('Factory image: 0x{0:X} bytes, boot at 0x00000000 and application at 0x{1:X8}.' -f $factorySize, $appStart)
Write-Output "Bootloader HEX: $bootHex"
Write-Output "Application BIN: $appBinary"
Write-Output "Factory HEX: $factoryHex"
Write-Output "Boot build log: $bootBuildLog"
Write-Output "App build log: $appBuildLog"
