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

# name -> extra source files it needs to link, and optional extra
# compiler flags. "Asan" builds it under AddressSanitizer: for code that
# parses bytes from the network, where an out-of-bounds read that lands
# on mapped memory passes every ordinary test.
$Tests = @(
    @{ Name = "histogram_accuracy"; Sources = @("src\core\FrameAnalyzer.cpp") }
    @{ Name = "adc_alignment";      Sources = @("src\core\ADCAlignment.cpp") }
    @{ Name = "sensor_analysis";    Sources = @("src\core\FrameAnalyzer.cpp") }
    @{ Name = "write_buffer_sizing"; Sources = @() }
    @{ Name = "sky_conditions";     Sources = @() }
    @{ Name = "capture_thread_submit"; Sources = @("src\core\CaptureManager.cpp", "src\core\WriteBuffer.cpp", "src\core\SERWriter.cpp") }
    @{ Name = "ai_context_facts";   Sources = @() }
    @{ Name = "object_knowledge";   Sources = @() }
    @{ Name = "object_panel_text"; Sources = @() }
    @{ Name = "slew_variable";     Sources = @() }
    @{ Name = "mount_jog";         Sources = @() }
    @{ Name = "planet_apparent_size"; Sources = @() }
    @{ Name = "profile_notes";     Sources = @() }
    @{ Name = "recentre";          Sources = @() }
    @{ Name = "observation_record"; Sources = @() }
    @{ Name = "hardware_link";     Sources = @() }
    @{ Name = "ws_protocol";       Sources = @("src\net\APEXTelemetry.cpp"); Asan = $true }
    @{ Name = "ws_fuzz";           Sources = @("src\net\APEXTelemetry.cpp"); Asan = $true }
    @{ Name = "ws_server_loopback"; Sources = @("src\net\WebSocketServer.cpp"); Asan = $true }
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

# ── sqlite3.dll beside the test binaries ────────────────────
#
# object_knowledge loads SQLite at runtime, exactly as the app does,
# and ObjectKnowledgeDB fails GRACEFULLY when the DLL is absent. So a
# missing DLL does not announce itself -- it just turns the whole
# knowledge base into "not available", which is the failure mode that
# hid an unread 2.4 MB database for months in the first place.
#
# This was passing only because the DLL had been copied here by hand
# during development. _build is gitignored, so on a clean checkout
# the gate failed. Copy it as part of the run.
$sqlite = Join-Path $root "external\sqlite3\lib\x64\sqlite3.dll"
if (Test-Path $sqlite) {
    Copy-Item $sqlite (Join-Path $out "sqlite3.dll") -Force
} else {
    Write-Host "[tests] WARNING: $sqlite not found - object_knowledge will fail"
}

# ── The AddressSanitizer runtime beside the test binaries ───
#
# Same reason as sqlite3.dll: an /fsanitize=address exe will not start
# without it, and it lives in the toolset's bin folder, not on PATH.
$asanDll = $null
$verFile = Join-Path $vsPath "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
if (Test-Path $verFile) {
    $ver = (Get-Content $verFile -TotalCount 1).Trim()
    $cand = Join-Path $vsPath "VC\Tools\MSVC\$ver\bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll"
    if (Test-Path $cand) {
        $asanDll = $cand
        # Skip an identical copy: a still-running ASan binary holds the
        # DLL open, and overwriting it would fail the whole gate.
        $dst = Join-Path $out (Split-Path $cand -Leaf)
        if (-not (Test-Path $dst) -or (Get-Item $dst).Length -ne (Get-Item $cand).Length) {
            Copy-Item $cand $dst -Force
        }
    }
}
if (-not $asanDll) {
    Write-Host "[tests] WARNING: AddressSanitizer runtime not found - ASan suites will fail to start"
}

foreach ($t in $Tests) {
    $name = $t.Name
    $src  = Join-Path $PSScriptRoot "$name.cpp"
    $extra = ($t.Sources | ForEach-Object { "`"$root\$_`"" }) -join " "
    $exe  = Join-Path $out "$name.exe"
    $flags = if ($t.Asan) { "/fsanitize=address /Zi /Fd:`"$out\$name.pdb`"" } else { "" }

    Write-Host ""
    Write-Host "--- building $name ---"
    $cmd = "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++20 /EHsc /O2 $flags $inc `"$src`" $extra /Fe:`"$exe`" /Fo:`"$out\\`""
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
