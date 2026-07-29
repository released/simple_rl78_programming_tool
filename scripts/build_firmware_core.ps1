param(
    [string]$Compiler = "C:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe",
    [string]$OutputDirectory = "demo_code\M032BSP_RL78_Programmer\SampleCode\RL78_Programmer\KEIL\obj\c90-check",
    [string]$NuvotonBspRoot = "demo_code\M032BSP_RL78_Programmer"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root $OutputDirectory
if (-not [System.IO.Path]::IsPathRooted($NuvotonBspRoot)) {
    $NuvotonBspRoot = Join-Path $root $NuvotonBspRoot
}

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "ARM Compiler 6 was not found: $Compiler"
}

New-Item -ItemType Directory -Force -Path $output | Out-Null

$common = @(
    "--target=arm-arm-none-eabi",
    "-mcpu=cortex-m0",
    "-std=c90",
    "-pedantic",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I$root\shared\include",
    "-I$root\firmware\core\include",
    "-I$root\firmware\m032_bridge",
    "-I$root\firmware\m032_app",
    "-I$root\firmware\board",
    "-I$root\firmware\storage",
    "-I$root\firmware\target",
    "-I$root\firmware\nulink2me",
    "-I$root\firmware\m032_test_board"
)

$sources = @(
    "shared\src\integrity.c",
    "shared\src\plp_frame.c",
    "shared\src\oip_format.c",
    "firmware\core\src\programmer_core.c",
    "firmware\m032_bridge\m032_programmer.c",
    "firmware\m032_app\m032_application.c",
    "firmware\storage\m032_package_store.c",
    "firmware\target\rl78_protocol_b.c",
    "firmware\target\m032_rl78_target_binding.c",
    "firmware\nulink2me\nulink2me_image_cache.c",
    "firmware\nulink2me\nulink2me_offline_recovery.c",
    "firmware\board\m032_board_profile.c",
    "firmware\board\m032_board_io.c",
    "firmware\board\m032_status_led.c"
)

foreach ($relative in $sources) {
    $source = Join-Path $root $relative
    $name = [System.IO.Path]::GetFileNameWithoutExtension($source)
    $object = Join-Path $output ($name + ".o")
    & $Compiler @common -c $source -o $object
    if ($LASTEXITCODE -ne 0) {
        throw "ARMCLANG failed for $relative"
    }
}

Write-Output "ARMCLANG strict C90 compile passed for $($sources.Count) portable firmware files."

$deviceInclude = Join-Path $NuvotonBspRoot "Library\Device\Nuvoton\M031\Include"
$driverInclude = Join-Path $NuvotonBspRoot "Library\StdDriver\inc"
$cmsisInclude = Join-Path $NuvotonBspRoot "Library\CMSIS\Core\Include"
if ((Test-Path -LiteralPath $deviceInclude) -and
    (Test-Path -LiteralPath $driverInclude) -and
    (Test-Path -LiteralPath $cmsisInclude)) {
    $boardCompile = @(
        "--target=arm-arm-none-eabi",
        "-mcpu=cortex-m0",
        "-std=gnu90",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I$root\shared\include",
        "-I$root\firmware\core\include",
        "-I$root\firmware\m032_bridge",
        "-I$root\firmware\m032_app",
        "-I$root\firmware\board",
        "-I$root\firmware\storage",
        "-I$root\firmware\target",
        "-I$root\firmware\m032_test_board",
        "-I$deviceInclude",
        "-I$driverInclude",
        "-I$cmsisInclude"
    )
    $vendorSources = @(
        "firmware\board\m032_numaker_m032se_board.c",
        "firmware\m032_app\m032_numicro_hid_port.c",
        "firmware\m032_app\m032_hid_descriptors.c",
        "firmware\m032_app\m032_iap_control.c",
        "firmware\storage\m032_numicro_fmc_storage.c",
        "firmware\m032_app\m032_numicro_main.c",
        "firmware\m032_test_board\m032_test_board_profile.c",
        "firmware\m032_test_board\m032_test_tool0_soft_uart.c",
        "firmware\m032_test_board\m032_test_board.c",
        "firmware\m032_test_board\m032_test_uart_port.c",
        "firmware\m032_test_board\m032_test_main.c"
    )
    foreach ($relative in $vendorSources) {
        $source = Join-Path $root $relative
        $name = [System.IO.Path]::GetFileNameWithoutExtension($source)
        $object = Join-Path $output ($name + ".o")
        & $Compiler @boardCompile -c $source -o $object
        if ($LASTEXITCODE -ne 0) {
            throw "ARMCLANG failed for $relative"
        }
    }
    Write-Output "ARMCLANG compile passed for $($vendorSources.Count) M032 NuMicro board adapters."
} else {
    Write-Warning "Nuvoton BSP not found; NuMaker register adapter compile was skipped: $NuvotonBspRoot"
}
