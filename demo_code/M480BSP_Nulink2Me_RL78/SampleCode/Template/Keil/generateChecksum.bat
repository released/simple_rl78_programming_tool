@echo off
setlocal ENABLEDELAYEDEXPANSION
pushd "%~dp0"

REM Nu-Link2-Me M484SIDAE bridge application layout:
REM   Bootloader        0x00000000 - 0x00003FFF
REM   Application image 0x00004000 - 0x0007FFFF
REM   Reserved CRC page 0x0007F000 - 0x0007FFFF
REM   CRC32 word        0x0007FFFC - 0x0007FFFF
set "APP_HEX=.\obj\NuLink2MeRl78Bridge.hex"
set "BOOT_HEX=..\..\ISP_HID_20\KEIL\obj\NuLink2MeIapBoot.hex"
set "SREC_EXE=srec_cat.exe"

set "APP_START=0x00004000"
set "APP_CRC_PAGE=0x0007F000"
set "APP_CRC_ADDR=0x0007FFFC"
set "APP_END_EXCL=0x00080000"
set "FLASH_START=0x00000000"
set "OUT_BLK=16"

set "APP_HEX_BAK=.\obj\NuLink2MeRl78Bridge_backup.hex"
set "APP_HEX_WITH_CRC=.\obj\NuLink2MeRl78Bridge_CRC.hex"
set "APP_BIN_WITH_CRC=.\obj\NuLink2MeRl78Bridge.bin"
set "MERGED_HEX=.\obj\NuLink2Me_boot_app.hex"
set "MERGED_BIN=.\obj\NuLink2Me_boot_app.bin"

where "%SREC_EXE%" >nul 2>&1
if errorlevel 1 (
  if exist "C:\Program Files (x86)\Texas Instruments\Fusion Digital Power Designer\bin\srec_cat.exe" (
    set "SREC_EXE=C:\Program Files (x86)\Texas Instruments\Fusion Digital Power Designer\bin\srec_cat.exe"
  ) else (
    echo [ERROR] srec_cat.exe was not found in PATH or the supported local installation.
    popd
    exit /b 2
  )
)

if not exist "%APP_HEX%" (
  echo [ERROR] Application HEX not found: "%APP_HEX%"
  popd
  exit /b 3
)

"%SREC_EXE%" "%APP_HEX%" -Intel ^
  -crop %APP_START% %APP_CRC_PAGE% ^
  -o "%APP_HEX_BAK%" -Intel -Output_Block_Size=%OUT_BLK%
if errorlevel 1 goto fail

"%SREC_EXE%" "%APP_HEX%" -Intel ^
  -crop %APP_START% %APP_CRC_PAGE% ^
  -fill 0xFF %APP_START% %APP_CRC_ADDR% ^
  -crc32-l-e %APP_CRC_ADDR% ^
  -crop %APP_START% %APP_END_EXCL% ^
  -o "%APP_HEX_WITH_CRC%" -Intel -Output_Block_Size=%OUT_BLK%
if errorlevel 1 goto fail

"%SREC_EXE%" "%APP_HEX_WITH_CRC%" -Intel ^
  -crop %APP_START% %APP_END_EXCL% ^
  -offset -%APP_START% ^
  -o "%APP_BIN_WITH_CRC%" -binary
if errorlevel 1 goto fail

"%SREC_EXE%" "%APP_HEX_WITH_CRC%" -Intel ^
  -crop %APP_START% %APP_END_EXCL% ^
  -o "%APP_HEX%" -Intel -Output_Block_Size=%OUT_BLK%
if errorlevel 1 goto fail

if exist "%BOOT_HEX%" (
  "%SREC_EXE%" "%BOOT_HEX%" -Intel "%APP_HEX%" -Intel ^
    -o "%MERGED_HEX%" -Intel -Output_Block_Size=%OUT_BLK%
  if errorlevel 1 goto fail
  "%SREC_EXE%" "%MERGED_HEX%" -Intel ^
    -fill 0xFF %FLASH_START% %APP_END_EXCL% ^
    -crop %FLASH_START% %APP_END_EXCL% ^
    -o "%MERGED_BIN%" -binary
  if errorlevel 1 goto fail
) else (
  echo [WARN] Bootloader HEX not found; merged factory image was not generated.
)

echo M484 bridge application CRC image generated successfully.
popd
exit /b 0

:fail
echo [ERROR] generateChecksum.bat failed.
popd
exit /b 10
