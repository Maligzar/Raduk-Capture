@echo off
REM launch_emulator.bat — Build if needed, then launch RadukCapture in emulator mode
REM Works from any folder: paths are relative to this script.
REM
REM Use this for day-to-day development when the telescope is packed away.

setlocal

set EXE=%~dp0build_emu\Release\RadukCapture.exe

REM 2026-11-09: always build first. This used to build only when the exe
REM was missing, so after a pull it went on launching the OLD exe -- a
REM launch crash was reported from a binary that predated a day of merges.
REM The build is incremental: with nothing changed it takes seconds.
echo.
echo  Bringing the emulator build up to date...
call "%~dp0build_emulator.bat"
if errorlevel 1 (
    echo.
    echo  *** Build failed - not launching an out-of-date exe. ***
    goto :end
)

echo.
echo  Launching RadukCapture in EMULATOR mode...
echo  ^(Camera + EAF + Mount all simulated^)
echo.
start "" "%EXE%"

:end
endlocal
