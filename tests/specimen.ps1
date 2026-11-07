# ============================================================
#  specimen.ps1 -- phase 132.2
#
#  Builds tools\specimen.cpp and writes data\specimen.html: today's
#  palette and the instrument candidates, side by side, in day and
#  night vision, with their contrast. Opens it with -Open.
#
#    .\tests\specimen.ps1 -Open
# ============================================================
param([switch]$Open)

$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $PSScriptRoot "_build"
$data  = Join-Path $root "data"
New-Item -ItemType Directory -Force -Path $build, $data | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Write-Host "[specimen] vswhere not found - is Visual Studio installed?"; exit 1 }
$vsPath = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$exe = Join-Path $build "specimen.exe"
$cmd = "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I `"$root\src`" " +
       "`"$root\tools\specimen.cpp`" /Fe:`"$exe`" /Fo:`"$build\\`" user32.lib gdi32.lib"
$log = cmd /c $cmd 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[specimen] did not compile:"
    $log | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" }
    exit 1
}
$out = Join-Path $data "specimen.html"
& $exe (Join-Path $root "assets\fonts") $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Open) { Start-Process $out }
