@echo off
REM Build RadukCapture in emulator mode — no hardware required.
REM Run from D:\RadukCapture

echo.
echo ========================================
echo  RadukCapture -- EMULATOR BUILD
echo ========================================
echo.

cd /d D:\RadukCapture

REM Wipe stale build dir so CMake picks up the correct generator fresh
if exist build_emu (
    echo Cleaning stale build_emu...
    rmdir /s /q build_emu
)

REM No -G flag — CMake auto-detects VS2026 (v18)
cmake -B build_emu -DRADUK_EMULATOR=ON -A x64

if %ERRORLEVEL% neq 0 (
    echo.
    echo CMake configure failed.
    pause
    exit /b 1
)

cmake --build build_emu --config Release

if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Done. Launching emulator...
echo ========================================
echo.

build_emu\Release\RadukCapture.exe
