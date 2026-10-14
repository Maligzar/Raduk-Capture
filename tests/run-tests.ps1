# ============================================================
#  RadukCapture test suite
#
#  Builds and runs every test listed below. Exits non-zero if any
#  fails, so it can gate a merge.
#
#  Adding a test: drop a .cpp in this folder whose main() returns 0
#  on success and non-zero on failure, then add one line to $Tests.
#
#  These run on the bench with no camera, mount or sky. Anything
#  needing hardware belongs in docs\CLEAR-NIGHT-CHECKLIST.md
#  instead, because it cannot gate anything.
# ============================================================

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $PSScriptRoot "_build"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# name -> extra source files it needs to link
$Tests = @(
    @{ Name = "histogram_accuracy"; Sources = @("src\core\FrameAnalyzer.cpp") }
    @{ Name = "adc_alignment";      Sources = @("src\core\ADCAlignment.cpp") }
    @{ Name = "sensor_analysis";    Sources = @("src\core\FrameAnalyzer.cpp") }
)

# ── Locate the compiler ─────────────────────────────────────
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Host "[tests] vswhere not found - is Visual Studio installed?"
    exit 1
}
$vsPath = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    Write-Host "[tests] vcvars64.bat not found under $vsPath"
    exit 1
}

$inc = "/I `"$root\src`" /I `"$root\external\ASI_SDK\include`""
$failed = 0

foreach ($t in $Tests) {
    $name = $t.Name
    $src  = Join-Path $PSScriptRoot "$name.cpp"
    $extra = ($t.Sources | ForEach-Object { "`"$root\$_`"" }) -join " "
    $exe  = Join-Path $out "$name.exe"

    Write-Host ""
    Write-Host "--- building $name ---"
    $cmd = "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++20 /EHsc /O2 $inc `"$src`" $extra /Fe:`"$exe`" /Fo:`"$out\\`""
    $buildLog = cmd /c $cmd 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] $name did not compile:"
        $buildLog | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" }
        $failed++
        continue
    }

    Write-Host "--- running $name ---"
    & $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] $name"
        $failed++
    } else {
        Write-Host "[OK]   $name"
    }
}

Write-Host ""
Write-Host "============================================================"
if ($failed -eq 0) {
    Write-Host " ALL $($Tests.Count) TEST(S) PASSED"
    Write-Host "============================================================"
    exit 0
} else {
    Write-Host " $failed of $($Tests.Count) TEST(S) FAILED"
    Write-Host "============================================================"
    exit 1
}
