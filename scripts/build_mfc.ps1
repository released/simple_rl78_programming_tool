param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [bool]$CloseRunningTool = $true
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($CloseRunningTool) {
    Get-Process -Name "Rl78ProgrammerTool" -ErrorAction SilentlyContinue | Stop-Process -Force
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere not found. Install Visual Studio 2022 with C++ and MFC."
}
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild not found."
}
& $msbuild (Join-Path $root "Rl78ProgrammerTool.vcxproj") /m /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
Write-Host "Built: $(Join-Path $root 'build\Rl78ProgrammerTool.exe')"
