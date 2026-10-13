@echo off
REM Thin wrapper so the test suite starts the same way as the builds.
REM The work is in run-tests.ps1, which handles locating the compiler
REM far more reliably than batch does.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-tests.ps1" %*
exit /b %ERRORLEVEL%
