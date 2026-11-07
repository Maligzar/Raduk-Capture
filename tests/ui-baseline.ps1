# ============================================================
#  ui-baseline.ps1 -- phase 132.0
#
#  Shows which windows a UI change touched, so a stage of phase 132 can
#  be held to what it said it would change.
#
#  1. On the build BEFORE the change (usually master):
#       .\tests\ui-baseline.ps1 -Save
#     Runs the ui-baseline scenario twice. The first run's shots become
#     the reference; the difference between the two runs becomes each
#     window's noise floor, so live windows (clocks, graphs, the sky
#     map) are not called changed for being alive.
#
#  2. Build the change, then:
#       .\tests\ui-baseline.ps1 -Expect "Histogram,Sharpness" -Open
#     Runs the scenario once and compares it with the reference. Any
#     window that changed and is not named in -Expect, or went missing,
#     or appeared, is a problem. -Open shows the contact sheet in the
#     browser.
#
#  Exit code: 0 when nothing unexpected changed; otherwise the number of
#  unexpected windows, or 100 when the run itself could not be made.
#
#  Everything it writes is under test_runs\, which is not committed:
#    test_runs\ui_reference\   shots, noise.txt, build.txt
#    test_runs\ui_diff\        index.html and heat\*.png
# ============================================================
param(
    [switch]$Save,
    [string]$Expect = "",
    [switch]$Open
)

$ErrorActionPreference = "Stop"
$root    = Split-Path -Parent $PSScriptRoot
$runs    = Join-Path $root "test_runs"
$refDir  = Join-Path $runs "ui_reference"
$diffDir = Join-Path $runs "ui_diff"
$build   = Join-Path $PSScriptRoot "_build"
New-Item -ItemType Directory -Force -Path $build, $runs | Out-Null

# ── Build the diff tool (only when its source is newer) ─────
$tool = Join-Path $build "shotdiff.exe"
$srcs = @("tools\shotdiff.cpp", "src\core\ShotDiff.h", "src\core\DashboardLaunch.h") |
        ForEach-Object { Join-Path $root $_ }
$stale = -not (Test-Path $tool)
if (-not $stale) {
    $built = (Get-Item $tool).LastWriteTime
    $stale = [bool]($srcs | Where-Object { (Get-Item $_).LastWriteTime -gt $built })
}
if ($stale) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { Write-Host "[ui] vswhere not found - is Visual Studio installed?"; exit 100 }
    $vsPath = & $vswhere -latest -property installationPath
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    $cmd = "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I `"$root\src`" " +
           "`"$root\tools\shotdiff.cpp`" /Fe:`"$tool`" /Fo:`"$build\\`" windowscodecs.lib ole32.lib"
    $log = cmd /c $cmd 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ui] shotdiff did not compile:"
        $log | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" }
        exit 100
    }
}

# ── One run of the scenario; returns its shots folder ───────
function Invoke-Baseline {
    $started = Get-Date
    & (Join-Path $PSScriptRoot "run-scenarios.ps1") "ui-baseline" | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host "[ui] the ui-baseline scenario did not pass; see its report.txt"; exit 100 }
    $box = Get-ChildItem $runs -Directory -Filter "ui-baseline_*" |
           Where-Object { $_.CreationTime -ge $started.AddSeconds(-2) } |
           Sort-Object CreationTime | Select-Object -Last 1
    if (-not $box) { Write-Host "[ui] no sandbox from the run"; exit 100 }
    $shots = Join-Path $box.FullName "shots"
    if (-not (Get-ChildItem $shots -Filter *.png -ErrorAction SilentlyContinue)) {
        Write-Host "[ui] the run wrote no shots in $shots"; exit 100
    }
    return $shots
}

$commit = (& git -C $root rev-parse --short HEAD 2>$null)
$branch = (& git -C $root rev-parse --abbrev-ref HEAD 2>$null)

if ($Save) {
    Write-Host "[ui] reference run 1 of 2"
    $a = Invoke-Baseline
    Write-Host "[ui] reference run 2 of 2, for the noise floor"
    $b = Invoke-Baseline

    if (Test-Path $refDir) { Remove-Item $refDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $refDir "shots") | Out-Null
    Copy-Item (Join-Path $a "*.png") (Join-Path $refDir "shots")
    & $tool $a $b (Join-Path $refDir "noise_run") --save-noise (Join-Path $refDir "noise.txt")
    $code = $LASTEXITCODE
    "commit $commit`r`nbranch $branch`r`nsaved  $(Get-Date -Format s)" |
        Set-Content (Join-Path $refDir "build.txt")
    if ($code -ne 0) {
        Write-Host "[ui] the two reference runs did not show the same windows. Saved anyway; diffs against it will be noisy."
        exit $code
    }
    Write-Host "[ui] reference saved from $branch at $commit"
    exit 0
}

if (-not (Test-Path (Join-Path $refDir "noise.txt"))) {
    Write-Host "[ui] no reference. Check out the build before the change and run: .\tests\ui-baseline.ps1 -Save"
    exit 100
}
Write-Host "[ui] reference:"
Get-Content (Join-Path $refDir "build.txt") | ForEach-Object { Write-Host "       $_" }
Write-Host "[ui] comparing $branch at $commit"

$now = Invoke-Baseline
if (Test-Path $diffDir) { Remove-Item $diffDir -Recurse -Force }
& $tool (Join-Path $refDir "shots") $now $diffDir --noise (Join-Path $refDir "noise.txt") --expect $Expect
$code = $LASTEXITCODE
if ($Open) { Start-Process (Join-Path $diffDir "index.html") }
exit $code
