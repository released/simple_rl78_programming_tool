param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere not found. Install Visual Studio 2022 C++ tools."
}
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild not found."
}
& $msbuild (Join-Path $root "Rl78ProgrammerTests.vcxproj") /m /p:Configuration=$Configuration /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
$exe = Join-Path $root "build\obj\tests\$Configuration\bin\Rl78ProgrammerTests.exe"
& $exe
exit $LASTEXITCODE
