# ============================================================
#  run-scenarios.ps1 -- phase 125
#
#  Runs the emulator build against each script in tests\scenarios and
#  says which passed. Each run gets its own sandbox under test_runs\,
#  with its report.txt, its log and any screenshots. Recordings are
#  deleted afterwards unless -Keep is given.
#
#    .\tests\run-scenarios.ps1              # every scenario
#    .\tests\run-scenarios.ps1 autorun      # those whose name contains it
#    .\tests\run-scenarios.ps1 -Keep        # keep the recordings
#
#  Exit code: the number that did not pass.
# ============================================================
param(
    [string]$Filter = "",
    [switch]$Keep
)

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "build_emu\Release\RadukCapture.exe"
if (-not (Test-Path $exe)) { Write-Host "No emulator build at $exe - run build_all.bat"; exit 1 }
if (Get-Process RadukCapture -ErrorAction SilentlyContinue) {
    Write-Host "RadukCapture is open. Close it first: a scenario will not run beside it."
    exit 1
}

# A leading _ marks a scenario known to fail, kept to show what is owed:
# run only when named.
$scripts = Get-ChildItem (Join-Path $PSScriptRoot "scenarios") -Filter *.scn |
           Where-Object { $_.BaseName -like "*$Filter*" -and
                          ($Filter -ne "" -or -not $_.BaseName.StartsWith("_")) } | Sort-Object Name
if (-not $scripts) { Write-Host "No scenario matches '$Filter'"; exit 1 }

$failed = 0
$rows = @()
foreach ($s in $scripts) {
    $limit = 300
    $t = Select-String -Path $s.FullName -Pattern '^\s*timeout\s+(\d+)' | Select-Object -First 1
    if ($t) { $limit = [int]$t.Matches[0].Groups[1].Value }
    $started = Get-Date
    $p = Start-Process -FilePath $exe -ArgumentList @("--scenario", "`"$($s.FullName)`"") -PassThru
    $done = $p.WaitForExit(($limit + 60) * 1000)
    if (-not $done) { $p.Kill(); $p.WaitForExit() }
    $secs = [int]((Get-Date) - $started).TotalSeconds

    $box = Get-ChildItem (Join-Path $root "test_runs") -Directory -Filter "$($s.BaseName)_*" -ErrorAction SilentlyContinue |
           Where-Object { $_.CreationTime -ge $started.AddSeconds(-2) } |
           Sort-Object CreationTime | Select-Object -Last 1
    $result = "FAIL"
    $why = ""
    if (-not $done) { $why = "hung past $($limit + 60) s and was killed" }
    elseif (-not $box) { $why = "no sandbox was made (exit code $($p.ExitCode))" }
    else {
        $rep = Join-Path $box.FullName "report.txt"
        if (Test-Path $rep) {
            $text = Get-Content $rep -Raw
            if ($text -match 'Result: (\w+)') { $result = $Matches[1] }
            if ($text -match 'Why: (.+)') { $why = $Matches[1].Trim() }
        } else { $why = "no report (exit code $($p.ExitCode))" }
        if (-not $Keep) {
            Get-ChildItem (Join-Path $box.FullName "captures") -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in ".ser", ".fits", ".fit" } | Remove-Item -Force
        }
    }
    if ($result -ne "PASS") { $failed++ }
    $rows += [pscustomobject]@{ Scenario = $s.BaseName; Result = $result; Seconds = $secs;
                                Sandbox = $(if ($box) { $box.Name } else { "" }); Why = $why }
}

$rows | Format-Table -AutoSize -Wrap
if ($failed) { Write-Host "$failed of $($scripts.Count) did not pass. Each sandbox's report.txt says why." }
else { Write-Host "All $($scripts.Count) passed." }
exit $failed
