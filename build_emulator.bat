@echo off
REM build_emulator.bat - configure and build the EMULATOR target.
REM
REM This file was 0 bytes. launch_emulator.bat calls it whenever the
REM emulator exe is missing, so that path silently did nothing and left
REM you staring at a launcher that had apparently not run.
REM
REM Emulator mode simulates camera, focuser and mount, so the app runs
REM with the telescope packed away.

setlocal
set CMAKE="D:\Program Files\CMake\bin\cmake.exe"
if not exist %CMAKE% set CMAKE=cmake

pushd "%~dp0"

echo.
echo === Configuring emulator build ===
%CMAKE% -S . -B build_emu -DRADUK_EMULATOR=ON
if errorlevel 1 goto :failed

echo.
echo === Building emulator ===
%CMAKE% --build build_emu --config Release
if errorlevel 1 goto :failed

echo.
echo Built: %~dp0build_emu\Release\RadukCapture.exe
popd
endlocal
exit /b 0

:failed
echo.
echo *** EMULATOR BUILD FAILED ***
popd
endlocal
exit /b 1
