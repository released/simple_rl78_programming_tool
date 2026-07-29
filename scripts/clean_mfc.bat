@echo off
setlocal
set "CLEAN_FAILED=0"

if /I "%~1"=="/?" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="--help" goto usage

pushd "%~dp0\.."

if not exist build mkdir build

if exist build\obj rd /s /q build\obj
if exist build\logs rd /s /q build\logs

if exist build\obj (
    echo WARNING: build\obj could not be completely removed. Close tools using it and retry.
    set "CLEAN_FAILED=1"
)
if exist build\logs (
    echo WARNING: build\logs could not be completely removed. Close the GUI and retry.
    set "CLEAN_FAILED=1"
)

if "%CLEAN_FAILED%"=="1" goto clean_failed

echo Clean complete. Preserved build\Rl78ProgrammerTool.exe and build\rl78_programmer.ini.

popd
endlocal
goto eof

:clean_failed
echo Clean incomplete. No preserved EXE or INI file was deleted.
popd
endlocal
exit /b 1

:usage
echo Usage: scripts\clean_mfc.bat
echo Removes only build\obj and build\logs.
echo Preserves build\Rl78ProgrammerTool.exe and build\rl78_programmer.ini.
endlocal

:eof
