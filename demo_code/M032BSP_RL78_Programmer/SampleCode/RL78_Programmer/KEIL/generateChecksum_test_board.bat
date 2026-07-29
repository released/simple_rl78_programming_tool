@echo off
setlocal ENABLEDELAYEDEXPANSION
pushd "%~dp0"

REM M032SE3AE APROM IAP layout:
REM   Bootloader : 0x00000000 - 0x00003FFF
REM   Application: 0x00004000 - 0x0000FBFF
REM   Reserved page: 0x0000FA00 - 0x0000FBFF
REM   CRC32 word : 0x0000FBFC - 0x0000FBFF
REM   Metadata   : 0x0000FC00 - 0x0000FFFF (not part of either image)
REM   Slot A     : 0x00010000 - 0x0001FFFF (not part of either image)

set "APP_HEX=.\obj\M032TestBoardBridge.hex"
set "BOOT_HEX=..\..\ISP_UART\KEIL\obj\ISP_UART.hex"
set "APP_START=0x00004000"
set "APP_CRC_ADDR=0x0000FBFC"
set "APP_END_EXCL=0x0000FC00"
set "BOOT_START=0x00000000"
set "BOOT_END_EXCL=0x00004000"
set "FACTORY_END_EXCL=0x0000FC00"
set "OUT_BLK=16"

set "APP_HEX_WITH_CRC=.\obj\M032TestBoardBridge_CRC.hex"
set "APP_BIN_WITH_CRC=.\obj\M032TestBoardBridge.bin"
set "FACTORY_HEX=.\obj\M032TestBoardBridge_factory.hex"
set "FACTORY_BIN=.\obj\M032TestBoardBridge_factory.bin"

set "SREC_EXE="
if defined SREC_CAT_EXE set "SREC_EXE=%SREC_CAT_EXE%"
if not defined SREC_EXE (
  for /f "delims=" %%I in ('where srec_cat.exe 2^>nul') do (
    if not defined SREC_EXE set "SREC_EXE=%%I"
  )
)

if not defined SREC_EXE (
  echo [ERROR] srec_cat.exe was not found. Install SRecord or set SREC_CAT_EXE.
  set "ERR=1"
  goto fail
)
if not exist "%SREC_EXE%" (
  echo [ERROR] SRecord executable was not found: "%SREC_EXE%"
  set "ERR=2"
  goto fail
)
if not exist "%APP_HEX%" (
  echo [ERROR] Application HEX was not found: "%APP_HEX%"
  set "ERR=3"
  goto fail
)

del /q "%APP_HEX_WITH_CRC%" "%APP_BIN_WITH_CRC%" "%FACTORY_HEX%" "%FACTORY_BIN%" >nul 2>&1

echo [1/4] Generate fixed-size application HEX with CRC32/ISO-HDLC.
"%SREC_EXE%" "%APP_HEX%" -Intel ^
  -crop %APP_START% %APP_CRC_ADDR% ^
  -fill 0xFF %APP_START% %APP_CRC_ADDR% ^
  -crc32-l-e %APP_CRC_ADDR% ^
  -crop %APP_START% %APP_END_EXCL% ^
  -o "%APP_HEX_WITH_CRC%" -Intel -Output_Block_Size=%OUT_BLK%
if errorlevel 1 (
  set "ERR=11"
  goto fail
)

echo [2/4] Generate application-only binary based at 0x00000000.
"%SREC_EXE%" "%APP_HEX_WITH_CRC%" -Intel ^
  -crop %APP_START% %APP_END_EXCL% ^
  -offset -%APP_START% ^
  -o "%APP_BIN_WITH_CRC%" -binary
if errorlevel 1 (
  set "ERR=12"
  goto fail
)

echo [3/4] Replace the Keil HEX with the checksummed application HEX.
copy /y "%APP_HEX_WITH_CRC%" "%APP_HEX%" >nul
if errorlevel 1 (
  set "ERR=13"
  goto fail
)

echo [4/4] Generate optional factory bootloader-plus-application image.
if exist "%BOOT_HEX%" (
  "%SREC_EXE%" "%BOOT_HEX%" -Intel ^
    -crop %BOOT_START% %BOOT_END_EXCL% ^
    "%APP_HEX_WITH_CRC%" -Intel ^
    -crop %APP_START% %APP_END_EXCL% ^
    -o "%FACTORY_HEX%" -Intel -Output_Block_Size=%OUT_BLK%
  if errorlevel 1 (
    set "ERR=14"
    goto fail
  )

  "%SREC_EXE%" "%FACTORY_HEX%" -Intel ^
    -fill 0xFF %BOOT_START% %FACTORY_END_EXCL% ^
    -crop %BOOT_START% %FACTORY_END_EXCL% ^
    -o "%FACTORY_BIN%" -binary
  if errorlevel 1 (
    set "ERR=15"
    goto fail
  )
) else (
  echo [WARN] Bootloader HEX was not found; factory image was not generated.
)

echo Application BIN: "%APP_BIN_WITH_CRC%"
if exist "%FACTORY_BIN%" echo Factory BIN    : "%FACTORY_BIN%"
popd
exit /b 0

:fail
echo [ERROR] generateChecksum.bat failed, code %ERR%.
popd
exit /b %ERR%
