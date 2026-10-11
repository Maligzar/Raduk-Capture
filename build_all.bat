@echo off
REM build_all.bat - build BOTH configurations and fail loudly if either breaks.
REM
REM Use this instead of building one target. The two configurations do not
REM compile the same code: RADUK_EMULATOR swaps the camera, focuser and
REM mount for simulated ones, and each pulls in a different set of headers.
REM
REM That difference is not theoretical. CaptureManager.h once used
REM std::function without including <functional>; the emulator build
REM compiled because another header happened to pull it in, the hardware
REM build did not, and because only the emulator was being built the
REM hardware build stayed broken across four phases without anyone
REM noticing. The rig would not have compiled on a clear night.
REM
REM Exit code is non-zero if either configuration fails, so this is safe
REM to use as a gate before committing.

setlocal
set CMAKE="D:\Program Files\CMake\bin\cmake.exe"
if not exist %CMAKE% set CMAKE=cmake

pushd "%~dp0"
set FAILED=

REM A running instance holds the exe and the link fails with LNK1104,
REM which reads as BUILD FAILED even though the code is fine. This gate
REM is only useful if a failure means a real failure.
taskkill /IM RadukCapture.exe /F >nul 2>&1

echo.
echo ============================================================
echo  1/2  HARDWARE build (real camera, focuser, mount)
echo ============================================================
%CMAKE% -S . -B build >nul
if errorlevel 1 set FAILED=%FAILED% configure-hardware
%CMAKE% --build build --config Release
if errorlevel 1 set FAILED=%FAILED% hardware

echo.
echo ============================================================
echo  2/2  EMULATOR build (simulated devices)
echo ============================================================
%CMAKE% -S . -B build_emu -DRADUK_EMULATOR=ON >nul
if errorlevel 1 set FAILED=%FAILED% configure-emulator
%CMAKE% --build build_emu --config Release
if errorlevel 1 set FAILED=%FAILED% emulator

echo.
if not "%FAILED%"=="" goto :failed
echo ============================================================
echo  BOTH CONFIGURATIONS BUILT
echo    hardware: %~dp0build\Release\RadukCapture.exe
echo    emulator: %~dp0build_emu\Release\RadukCapture.exe
echo ============================================================
popd
endlocal
exit /b 0

:failed
echo ============================================================
echo  *** BUILD FAILED:%FAILED% ***
echo  Do not commit until both configurations build.
echo ============================================================
popd
endlocal
exit /b 1
