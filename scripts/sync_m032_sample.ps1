param(
    [switch]$Check
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sampleRoot = Join-Path $root "demo_code\M032BSP_RL78_Programmer\SampleCode\RL78_Programmer"
$sources = @()

$sources += Get-ChildItem -LiteralPath (Join-Path $root "firmware") -Recurse -File |
    Where-Object {
        $_.Extension -in @(".c", ".h") -and
        $_.FullName -notlike "*\m032_app\keil\*"
    }
$sources += Get-ChildItem -LiteralPath (Join-Path $root "shared\include\rl78_programmer") -File -Filter *.h
$sources += Get-ChildItem -LiteralPath (Join-Path $root "shared\src") -File |
    Where-Object { $_.Name -in @("integrity.c", "plp_frame.c", "oip_format.c") }

$expected = @{}
$updated = 0
foreach ($source in $sources) {
    $relative = $source.FullName.Substring($root.Length + 1)
    $destination = Join-Path $sampleRoot $relative
    $expected[[System.IO.Path]::GetFullPath($destination)] = $true
    if ($Check) {
        if (-not (Test-Path -LiteralPath $destination)) {
            throw "M032 sample mirror is missing: $relative"
        }
        $sourceHash = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -ne $destinationHash) {
            throw "M032 sample mirror differs from canonical source: $relative"
        }
    } else {
        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
        if (-not (Test-Path -LiteralPath $destination) -or
            (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
            Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
            $updated++
        }
    }
}

$mirrored = Get-ChildItem -LiteralPath $sampleRoot -Recurse -File |
    Where-Object {
        $_.Extension -in @(".c", ".h") -and
        $_.FullName -notlike "*\KEIL\obj\*" -and
        $_.FullName -notlike "*\KEIL\lst\*"
    }
foreach ($file in $mirrored) {
    if (-not $expected.ContainsKey([System.IO.Path]::GetFullPath($file.FullName))) {
        throw "Unexpected C/H file in M032 sample mirror: $($file.FullName)"
    }
}

if ($Check) {
    Write-Output "M032 sample mirror check passed for $($sources.Count) canonical C/H files."
} else {
    Write-Output "M032 sample mirror synchronized: $updated updated, $($sources.Count) total C/H files."
}
