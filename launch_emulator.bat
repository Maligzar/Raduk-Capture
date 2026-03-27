@echo off
REM launch_emulator.bat — Launch RadukCapture in emulator mode (no rebuild)
REM Run from D:\RadukCapture
REM
REM Use this for day-to-day development when the telescope is packed away.
REM If the emulator exe is missing or stale, run build_emulator.bat first.

setlocal

set EXE=D:\RadukCapture\build_emu\Release\RadukCapture.exe

if not exist "%EXE%" (
    echo.
    echo  Emulator exe not found: %EXE%
    echo  Building now...
    echo.
    call "%~dp0build_emulator.bat"
    goto :end
)

echo.
echo  Launching RadukCapture in EMULATOR mode...
echo  ^(Camera + EAF + Mount all simulated^)
echo.
start "" "%EXE%"

:end
endlocal
