@echo off
REM publish-wiki.bat - publish wiki\*.md to the GitHub wiki.
REM
REM The GitHub wiki is a SEPARATE git repository (<repo>.wiki.git) with no
REM REST API, so publishing is a clone, copy, commit, push. This script is
REM the Windows entry point; publish-wiki.sh is the same thing for bash.
REM
REM Usage, from anywhere:
REM   wiki\publish-wiki.bat             publish
REM   wiki\publish-wiki.bat --dry-run   show what would change, push nothing
REM
REM The wiki must already exist. If it has never been initialised, create one
REM page through the web UI first:
REM   https://github.com/Maligzar/Raduk-Capture/wiki
REM
REM Override the target with WIKI_REPO or WIKI_URL.

setlocal

if "%WIKI_REPO%"=="" set WIKI_REPO=Maligzar/Raduk-Capture
if "%WIKI_URL%"==""  set WIKI_URL=https://github.com/%WIKI_REPO%.wiki.git

REM %~dp0 is guaranteed to end with a backslash, so strip it unconditionally
REM rather than testing for it -- the "\" literal that test needs is a cmd
REM parser hazard, and the test buys nothing.
set SRC=%~dp0
set SRC=%SRC:~0,-1%

set DRYRUN=
if /I "%~1"=="--dry-run" set DRYRUN=1

set WORK=%TEMP%\raduk-wiki-%RANDOM%%RANDOM%
if exist "%WORK%" rmdir /S /Q "%WORK%"

echo ==^> Cloning %WIKI_URL%
git clone --quiet "%WIKI_URL%" "%WORK%"
if errorlevel 1 goto :clonefailed

echo ==^> Copying pages

REM Remove pages the wiki has that wiki\ no longer defines, so a rename here
REM does not leave an orphan page behind.
for %%F in ("%WORK%\*.md") do (
    if not exist "%SRC%\%%~nxF" (
        echo     - removing stale page: %%~nxF
        git -C "%WORK%" rm --quiet -- "%%~nxF"
    )
)

REM README.md documents the wiki source directory; it is not a wiki page.
for %%F in ("%SRC%\*.md") do (
    if /I not "%%~nxF"=="README.md" copy /Y "%%F" "%WORK%\%%~nxF" >nul
)

git -C "%WORK%" add -A
if errorlevel 1 goto :failed

git -C "%WORK%" diff --cached --quiet
if errorlevel 1 goto :haschanges
echo ==^> No changes; wiki is already up to date.
goto :cleanup

:haschanges
echo ==^> Changes to publish:
git -C "%WORK%" diff --cached --stat

if defined DRYRUN (
    echo ==^> Dry run; nothing pushed.
    goto :cleanup
)

git -C "%WORK%" -c user.name=wiki-publish -c user.email=wiki-publish@localhost commit --quiet -m "Publish wiki from wiki/"
if errorlevel 1 goto :failed

echo ==^> Pushing
git -C "%WORK%" push --quiet origin HEAD
if errorlevel 1 goto :failed

echo ==^> Done: https://github.com/%WIKI_REPO%/wiki
goto :cleanup

:clonefailed
echo.
echo ERROR: could not clone the wiki repository.
echo        If the wiki has never been initialised, create one page here first:
echo        https://github.com/%WIKI_REPO%/wiki
goto :cleanupfail

:failed
echo.
echo ERROR: publishing failed. The working clone is left at:
echo        %WORK%
endlocal
exit /b 1

:cleanup
if exist "%WORK%" rmdir /S /Q "%WORK%"
endlocal
exit /b 0

:cleanupfail
if exist "%WORK%" rmdir /S /Q "%WORK%"
endlocal
exit /b 1
