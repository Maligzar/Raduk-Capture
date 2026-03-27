@echo off
REM kill_radukcapture.bat — Force-kill all RadukCapture instances
REM Also clears stale mutex handles left by crashes or force-kills.
REM Use when the app refuses to launch ("already running" error).

echo.
echo Killing all RadukCapture.exe instances...

taskkill /F /IM RadukCapture.exe /T >nul 2>&1

if %ERRORLEVEL% EQU 0 (
    echo Killed.
) else (
    echo No RadukCapture instances found.
)

REM Brief pause lets Windows release kernel mutex handles
timeout /t 1 /nobreak >nul

echo Ready to relaunch.
echo.
