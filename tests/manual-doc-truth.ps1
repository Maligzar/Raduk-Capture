# ============================================================
#  Do the documents still say true things?
#
#  This project's planning documents drifted badly enough to be worth a
#  tool. At one point phase 77 carried FOUR different statuses at once --
#  "it works, closed", "off the list, not ours", "known broken, reboot
#  then reinstall", and "currently blocked" -- across six files, because
#  each fix was a hand edit to one copy. A section heading on master even
#  contradicted its own body six lines below it.
#
#  None of that is hard to detect. It is only hard to remember to look.
#
#  MANUAL, and deliberately not in run-tests.ps1. It reads prose, so it
#  will report things that are fine and miss things that are not, and a
#  merge gate that cries wolf gets ignored or disabled. Run it after a
#  phase lands, or before a review.
#
#    powershell -ExecutionPolicy Bypass -File tests\manual-doc-truth.ps1
#
#  Exit code is 0 unless -Strict is passed, so it can be read rather
#  than obeyed.
# ============================================================

param([switch]$Strict)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$findings = 0

function Say($level, $what, $detail) {
    $script:findings++
    Write-Host ("  [{0}] {1}" -f $level, $what)
    if ($detail) { Write-Host ("        {0}" -f $detail) }
}

Write-Host ""
Write-Host "=== Document truth check ==="

# ── 1. Commit hashes that no longer resolve ───────────────────
#
# A squashed PR takes its individual commits with it. The roadmap cited
# 95c3048 twice after PR #15 was squashed into 202822f -- a hash that
# resolves in the clone that made it and nowhere else.
Write-Host ""
Write-Host "1. Commit hashes cited in the documents"
$docs = @(
    "docs\ROADMAP-Next.md", "RadukCapture-Architecture.md",
    "RadukCapture-Help.md", "docs\NEXT-SESSION.md",
    "docs\CLEAR-NIGHT-CHECKLIST.md"
) + (Get-ChildItem (Join-Path $root "wiki") -Filter *.md -ErrorAction SilentlyContinue |
     ForEach-Object { "wiki\" + $_.Name })

# Documents about OTHER projects cite other projects' commits -- the
# comparison report measured a N.I.N.A. clone and quotes its hash. Those
# will never be ancestors of our master, and reporting them as stale is
# the tool crying wolf, which is how a checker gets switched off. This is
# a rule rather than a guess at the surrounding prose: a file either is
# about our repository or it is not.
$foreignDocs = @(
    "wiki\Comparison-With-Other-Software.md"
)

$seen = @{}
foreach ($d in $docs) {
    if ($foreignDocs -contains $d) { continue }
    $p = Join-Path $root $d
    if (-not (Test-Path $p)) { continue }
    $n = 0
    foreach ($line in Get-Content $p) {
        $n++
        foreach ($m in [regex]::Matches($line, '`([0-9a-f]{7,40})`')) {
            $h = $m.Groups[1].Value
            # Hex-looking words that are not hashes (sizes, colours).
            if ($h -match '^[0-9]+$') { continue }
            $key = "$h"
            if (-not $seen.ContainsKey($key)) {
                # Not every hex word in a document is a commit. git
                # writes to stderr and returns non-zero for ones that
                # are not, and with ErrorActionPreference Stop that
                # aborts the whole run -- so ask whether the object
                # exists first, quietly, and only then whether it is
                # an ancestor.
                $prev = $ErrorActionPreference
                $ErrorActionPreference = "SilentlyContinue"
                & git -C $root cat-file -e ($h + "^{commit}") 2>$null
                $isCommit = ($LASTEXITCODE -eq 0)
                if ($isCommit) {
                    & git -C $root merge-base --is-ancestor $h origin/master 2>$null
                    $seen[$key] = ($LASTEXITCODE -eq 0)
                } else {
                    # Unknown to this clone. That is the WORSE case, not
                    # the safer one: a citation nobody can resolve is
                    # exactly what a squashed-away hash looks like to
                    # anyone who clones fresh. Length decides whether it
                    # is a citation at all -- cited short hashes in this
                    # repo run 7 to 12 characters, and a longer hex word
                    # is something else (a fault bucket, an id).
                    $seen[$key] = -not ($h.Length -ge 7 -and $h.Length -le 12)
                }
                $ErrorActionPreference = $prev
            }
            if (-not $seen[$key]) {
                Say "STALE" "$h is not an ancestor of origin/master" "$d`:$n"
            }
        }
    }
}
if ($findings -eq 0) { Write-Host "   every cited hash is in master" }

# ── 2. A phase section whose heading argues with its own body ──
#
# The exact defect found on master: a heading saying UNDER RE-TEST over
# a body saying it works.
Write-Host ""
Write-Host "2. Headings against their own bodies"
$before = $findings
$arch = Join-Path $root "RadukCapture-Architecture.md"
if (Test-Path $arch) {
    $lines = Get-Content $arch
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^#{2,4}\s+Phase\s') { continue }
        $head = $lines[$i]
        # The status line is the first **Status...** within 12 lines.
        $body = ""
        for ($j = $i + 1; $j -lt [Math]::Min($i + 13, $lines.Count); $j++) {
            if ($lines[$j] -match '^\*\*Status') { $body = $lines[$j]; break }
            if ($lines[$j] -match '^#{2,4}\s') { break }
        }
        if (-not $body) { continue }
        $headDone = $head -match '(?i)COMPLETE|SHIPPED|DONE'
        $headOpen = $head -match '(?i)RE-TEST|UNDER|BLOCKED|NOT WORKING|BROKEN'
        $bodyDone = $body -match '(?i)\bworks\b|verified|re-measured|shipped'
        $bodyOpen = $body -match '(?i)does not work|broken|withdrawn|unresolved|not verified'
        if (($headOpen -and $bodyDone) -or ($headDone -and $bodyOpen)) {
            Say "CONFLICT" "heading and body disagree" `
                ("{0}:{1}`n        head: {2}`n        body: {3}" -f `
                 "RadukCapture-Architecture.md", ($i + 1), $head.Trim(), $body.Substring(0, [Math]::Min(90, $body.Length)))
        }
    }
}
if ($findings -eq $before) { Write-Host "   no heading argues with its own body" }

# ── 3. One phase, two statuses, different files ───────────────
Write-Host ""
Write-Host "3. The same phase, described differently in different files"
$before = $findings
$status = @{}
foreach ($d in $docs) {
    $p = Join-Path $root $d
    if (-not (Test-Path $p)) { continue }
    $n = 0
    foreach ($line in Get-Content $p) {
        $n++
        $m = [regex]::Match($line, '(?i)phase\s+(\d+[a-c]?)\b')
        if (-not $m.Success) { continue }
        $ph = $m.Groups[1].Value
        # A line that QUOTES an old claim in order to withdraw it is
        # not an assertion of that claim. Without this the tool reports
        # its own corrections as contradictions, which is the fastest
        # way to get a checker ignored.
        if ($line -match '(?i)withdrawn|used to say|no longer true|corrected 20|earlier "|was "') { continue }

        $verdict = $null
        if ($line -match '(?i)does not work|known broken|not ours|currently blocked') { $verdict = "broken" }
        elseif ($line -match '(?i)✅|COMPLETE|SHIPPED|\bDONE\b')                   { $verdict = "done" }
        elseif ($line -match '(?i)\uD83D\uDD32|planned|unstarted|not built')             { $verdict = "planned" }
        if (-not $verdict) { continue }
        if (-not $status.ContainsKey($ph)) { $status[$ph] = @{} }
        if (-not $status[$ph].ContainsKey($verdict)) { $status[$ph][$verdict] = "$d`:$n" }
    }
}
foreach ($ph in ($status.Keys | Sort-Object { [int]($_ -replace '[a-c]','') })) {
    if ($status[$ph].Count -le 1) { continue }
    $where = ($status[$ph].GetEnumerator() | ForEach-Object { "{0} ({1})" -f $_.Key, $_.Value }) -join "`n        "
    Say "SPLIT" "phase $ph is described $($status[$ph].Count) ways" $where
}
if ($findings -eq $before) { Write-Host "   no phase carries two statuses" }

# ── 4. Cross-references to things that do not exist ───────────
#
# "clear-night item 14" was cited by two documents and existed in none.
Write-Host ""
Write-Host "4. Cross-references"
$before = $findings
$checklist = Join-Path $root "docs\CLEAR-NIGHT-CHECKLIST.md"
if (Test-Path $checklist) {
    $items = @{}
    $dupes = @()
    foreach ($line in Get-Content $checklist) {
        $m = [regex]::Match($line, '^##\s+(\d+)\.')
        if ($m.Success) {
            $k = $m.Groups[1].Value
            if ($items.ContainsKey($k)) { $dupes += $k } else { $items[$k] = $true }
        }
    }
    foreach ($d in $dupes) { Say "DUPLICATE" "clear-night item $d has two headings" "docs\CLEAR-NIGHT-CHECKLIST.md" }
    foreach ($d in $docs) {
        $p = Join-Path $root $d
        if (-not (Test-Path $p)) { continue }
        $n = 0
        foreach ($line in Get-Content $p) {
            $n++
            foreach ($m in [regex]::Matches($line, '(?i)clear-night item (\d+)')) {
                $k = $m.Groups[1].Value
                if (-not $items.ContainsKey($k)) {
                    Say "ORPHAN" "clear-night item $k is cited but has no heading in the checklist" "$d`:$n"
                }
            }
        }
    }
}
if ($findings -eq $before) { Write-Host "   every cited clear-night item exists" }

# ── 5. Source files nobody documented ─────────────────────────
Write-Host ""
Write-Host "5. Source files missing from the code map"
$before = $findings
$codeMap = Join-Path $root "wiki\Code-Map.md"
if (Test-Path $codeMap) {
    $map = Get-Content $codeMap -Raw
    # core AND gui. The first version looked only at src\core and so
    # reported four missing files when the real number was sixteen --
    # a check that is narrower than the claim it is testing gives
    # false confidence, which is worse than no check.
    foreach ($dir in @("core", "gui")) {
        foreach ($f in Get-ChildItem (Join-Path $root "src\$dir") -Filter *.h) {
            if ($map -notmatch [regex]::Escape($f.Name)) {
                Say "UNDOCUMENTED" "$($f.Name) is not in wiki\Code-Map.md" "src\$dir\$($f.Name)"
            }
        }
    }
}
if ($findings -eq $before) { Write-Host "   every src\core and src\gui header appears in the code map" }

Write-Host ""
Write-Host "============================================================"
if ($findings -eq 0) {
    Write-Host " NOTHING TO REPORT"
    exit 0
}
Write-Host " $findings thing(s) to look at"
Write-Host " Prose, not proof: read each one before believing it."
Write-Host "============================================================"
if ($Strict) { exit 1 }
exit 0
