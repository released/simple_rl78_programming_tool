param([ValidateSet('Debug','Release')][string]$Configuration='Release',
      [ValidateSet('Pc','Ui')][string]$Suite='Pc')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# Standalone checkouts use a fresh project-local owned run. The center is
# read-only in the managed session; formal output and personal INI/device
# state are never used by this runner.
$runRoot=Join-Path $projectRoot ('build\obj\pc-contract\'+[Guid]::NewGuid().ToString('N'))
$evidence=Join-Path $projectRoot ('docs\internal\'+$Suite.ToLowerInvariant()+'-contract-'+$Configuration.ToLowerInvariant()+'.json')
$targetName=if ($Suite -eq 'Ui') { 'Rl78ProgrammerUiTests' } else { 'Rl78ProgrammerPcTests' }
$owned=@()
$removed=0L
$failure=$null
$output=@()
$buildExit=$null
$testExit=$null
$exeHash=$null
$ownsRun=$false
try {
    $ancestor=Split-Path $runRoot
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Reparse ancestor refused.'
        }
        $ancestor=[IO.Path]::GetDirectoryName($ancestor)
    }
    if (Test-Path -LiteralPath $runRoot) { throw 'Run identity already exists.' }
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $ownsRun=$true
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild=& $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (!$msbuild) { throw 'MSBuild not found.' }
    $sdkInclude=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    $sdkVersion=Get-ChildItem -LiteralPath $sdkInclude -Directory | Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
    if (!$sdkVersion) { throw 'Windows 10/11 SDK not found.' }
    $sdkRoot=Split-Path $sdkInclude
    $buildOutput=& $msbuild (Join-Path $projectRoot ($targetName+'.vcxproj')) /m /nologo /v:minimal /p:Configuration=$Configuration /p:Platform=x64 "/p:TargetName=$targetName" "/p:WindowsTargetPlatformVersion=$sdkVersion" "/p:_LatestWindowsTargetPlatformVersion=$sdkVersion" "/p:TargetPlatformSdkPath=$sdkRoot" /p:TargetPlatformDisplayName=WindowsSDK "/p:OutDir=$runRoot\bin\" "/p:IntDir=$runRoot\obj\" 2>&1
    $buildExit=$LASTEXITCODE
    $buildOutput | Write-Host
    if ($buildExit -ne 0) { throw "PC contract build failed: $buildExit" }
    $exe=Join-Path $runRoot ('bin\'+$targetName+'.exe')
    $exeHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $stdout=Join-Path $runRoot 'stdout.log'
    $stderr=Join-Path $runRoot 'stderr.log'
    $process=Start-Process -FilePath $exe -ArgumentList ('"'+$runRoot+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null=$process.Handle
    $ownedPid=$process.Id
    $ownedStart=$process.StartTime.ToUniversalTime().Ticks
    if (!$process.WaitForExit(20000)) {
        $current=Get-Process -Id $ownedPid -ErrorAction Stop
        if ($current.Path -ceq $exe -and $current.StartTime.ToUniversalTime().Ticks -eq $ownedStart) {
            Stop-Process -Id $ownedPid
            $process.WaitForExit()
        }
        throw 'Test-owned process exceeded the bounded fixture deadline.'
    }
    $process.WaitForExit()
    $process.Refresh()
    $testExit=$process.ExitCode
    $output=@(Get-Content -LiteralPath $stdout -Encoding UTF8) + @(Get-Content -LiteralPath $stderr -Encoding UTF8)
    $output | Write-Host
    if ($testExit -ne 0) { throw "PC contract checks failed: $testExit" }
    if (!@($output | Where-Object { "$_" -match 'contract checks passed: [1-9][0-9]*$' }).Count) { throw 'Fixture entry point did not produce its acceptance summary.' }
} catch { $failure=$_.Exception.Message }
finally {
    $cleanupFailure=$null
    if ($ownsRun) {
    # The fresh run belongs exclusively to the above build and synthetic test.
    # Register exact produced files before retirement; preserve unknown additions.
    $allowed=@('.obj','.pdb','.ilk','.idb','.iobj','.ipdb','.tlog','.lastbuildstate','.log','.recipe','.exe','.mot','.res')
    $files=@(Get-ChildItem -LiteralPath $runRoot -Recurse -File -ErrorAction SilentlyContinue)
    foreach ($file in $files) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { $cleanupFailure='Reparse artifact retained.'; break }
        if (($file.Extension -notin $allowed -and $file.Name -ne 'unsuccessfulbuild' -and $file.Name -ne 'rl78_programmer.ini' -and $file.Name -ne 'synthetic.bin') -or
            ($file.Extension -eq '.exe' -and $file.Name -ne ($targetName+'.exe')) -or
            ($file.Extension -eq '.mot' -and $file.Name -ne 'synthetic.mot')) {
            $cleanupFailure='Unknown run artifact retained: '+$file.Name; break
        }
        $owned+=@{Path=$file.FullName; Bytes=$file.Length; Sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    }
    if ($owned.Count -eq $files.Count) {
        foreach ($file in $owned) {
            if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.Sha256) { throw 'Artifact changed before retirement.' }
            Remove-Item -LiteralPath $file.Path
            $removed+=$file.Bytes
        }
        Get-ChildItem -LiteralPath $runRoot -Recurse -Directory | Sort-Object FullName -Descending | ForEach-Object {
            if (@(Get-ChildItem -LiteralPath $_.FullName -Force).Count -eq 0) { Remove-Item -LiteralPath $_.FullName }
        }
        if (@(Get-ChildItem -LiteralPath $runRoot -Force).Count -eq 0) { Remove-Item -LiteralPath $runRoot }
    }
    }
    $record=[ordered]@{Configuration=$Configuration; BuildExit=$buildExit; TestExit=$testExit; ExeSha256=$exeHash; Output=@($output | ForEach-Object { "$_" }); RemovedBytes=$removed; RunRoot=$runRoot; RunRemoved=!(Test-Path -LiteralPath $runRoot); Error=$failure; CleanupError=$cleanupFailure}
    New-Item -ItemType Directory -Path (Split-Path $evidence) -Force | Out-Null
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $evidence -Encoding UTF8
}
if ($failure) { throw $failure }
if ($cleanupFailure) { throw $cleanupFailure }
if (Test-Path -LiteralPath $runRoot) { throw 'Test run retirement incomplete.' }
