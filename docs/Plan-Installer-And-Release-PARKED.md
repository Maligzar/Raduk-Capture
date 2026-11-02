# The installer and the first release — PARKED

**Parked by the owner, 2026-11-01. No phase number.** A release may be
months or years away, and adoption is not a goal. This stays as the
worked plan for when it is wanted. Its findings about the build folder
hold regardless: `rig.ini` carries the real site and `WsToken`, so nothing
is ever packaged from that folder. The licence question in section 6 is
answered: PolyForm Noncommercial stays.

**v3.0 went out as a source release through phase 114, with no installer or
binaries.** Everything below is still parked. The 2.5.0 in this plan is out
of date, and the next packaged version needs its own number.

P5.4's remaining halves. The paths half shipped on 2026-10-26. Chosen on
2026-11-01 because Distribution carries 15% of the comparison's weight at
a score of 1, and it is the largest gap that needs neither sky nor new
hardware. Each point there is worth 0.15.

**State when written, 2026-11-01:** `PLANNED`. There is no packaging
script, no installer and no release artifact. `v2.0` points far behind
`master`, so a release needs a new tag.

---

## 1. What the tree decides before any design does

Read from the tree, not assumed.

| Finding | Where | Consequence |
|---|---|---|
| `build\Release\rig.ini` holds the real site, and `WsToken` lives in the same file | `RigConfig.h`, the file's own comment | **Packaging must be an allowlist.** Zipping the build folder would publish the observing site and the access token |
| `rig.ini` sits beside the exe | `RigConfig::IniPath` | The install folder has to be writable, so `Program Files` is out unless this moves |
| The data root falls back to the exe's folder | `Paths.h`, step 3 | Captures would pile up in the install folder, and an uninstall could reach them |
| `D:\RadukCapture` wins whenever it exists | `Paths.h`, step 2 | **This machine cannot test a fresh install.** It always takes the migration branch |
| `workspace.ini` is opened by a relative path | `main.cpp:1503` | Its location depends on the working directory, so a shortcut or a double-click can lose the layout |
| The exe needs `MSVCP140`, `VCRUNTIME140` and `VCRUNTIME140_1` | `dumpbin /dependents` | A clean machine may not start it at all. The runtime has to travel with it |
| `data/pointing_model.csv` is tracked, and `profiles/` has local edits | `git ls-files`, `git status` | Files must come from the tagged commit, never the working tree. `pointing_model.csv` is one rig's data and does not ship |
| No licence file for either ZWO SDK in `external/` | `find external` | The right to redistribute the two DLLs cannot be read from this repository |
| `kCaptureInstrument` and `kCaptureTelescope` name this rig | `CaptureWriter.h` | Someone else's SER and FITS files would claim an 8SE |
| Windows Sandbox is not enabled here | `WindowsSandbox.exe` absent | A clean-machine test needs another PC or a VM |

---

## 2. Decisions

**An allowlist, built from the tag.** `tools/package.ps1` takes every file
from the tagged commit or a fresh build of it, and copies only what is
named. Anything unlisted does not ship, which is the only rule that keeps
`rig.ini` out by construction.

**A per-user install, no administrator.** To
`%LOCALAPPDATA%\Programs\RadukCapture`. `rig.ini` beside the exe stays
writable, and nothing has to move. Moving settings to `%APPDATA%` is the
tidier answer and also a migration of this rig's only config, for no
gain a user would see.

**The data folder is chosen at install, and uninstall never touches it.**
The installer asks, defaulting to `Documents\RadukCapture`, and writes
`DataRoot`. That matters more than usual here: a night of SER files is
tens of GB and may belong on a different drive. Profiles and the knowledge
database are seeded there, and an existing file is never overwritten.

**An upgrade never overwrites `rig.ini` or `workspace.ini`.** Neither
ships, so an upgrade cannot reach them. The defaults come from
`RigConfig`, and first run already creates what it needs.

**The VC++ runtime goes in app-local.** The three DLLs from Visual
Studio's `VC\Redist` folder sit beside the exe. It is permitted, it needs
no administrator, and it keeps the portable zip working on its own.

**Both builds ship.** `RadukCapture.exe` for hardware and
`RadukCapture-Emulator.exe`, with a shortcut each. The emulator is the
project's clearest differentiator, and it is how someone with no ZWO
camera can try the application at all. They share the folder and
`rig.ini`, so a setting made in one is true in the other.

**Inno Setup.** A single small script, a well-worn per-user install and
free for this use. It adds a build tool to this machine, and only the
release step needs it. The zip does not.

**The instrument names come from the rig.** `INSTRUME` from the camera's
own `Describe()` name, and `TELESCOP` from a `rig.ini` value, empty by
default and then omitted. The SER header leaves the field blank. This
machine sets its own value once, and the `Observer` default of `Raduk`
goes in the same change, which is the owner's 2026-11-01 rule about names
reaching SER as well.

**The version is 2.5.0.** FITS is a user-visible format, and the tag must
match `src/Version.h`.

**Publishing is the owner's step.** The script ends at a draft GitHub
Release with the files attached. Making it public is a click nobody else
makes.

---

## 3. Stages

**107.1 · Make it installable.** `workspace.ini` beside the exe rather
than wherever the working directory happens to be. Instrument and
telescope from the rig. Blank `Observer`. A first run on an empty folder
reaches a usable window with no `rig.ini` present.

**107.2 · The package.** `tools/package.ps1`: refuses a dirty tree or an
untagged commit, builds both configurations, staged by allowlist, and
writes `RadukCapture-<version>-portable.zip` and a `NOTICE.txt`.

**107.3 · The installer.** `tools/installer.iss` over the same staged
folder: the data-folder page, `DataRoot`, seeding, shortcuts, an
uninstaller that leaves the data folder and says so.

**107.4 · A clean machine.** Section 5.

**107.5 · The release.** `src/Version.h` to 2.5.0, the help's version
table, the tag, the draft Release, and a README section on installing.

---

## 4. What gates it

**`tests/package_manifest.ps1`,** run by `package.ps1` over the staged
folder, and able to fail the release:

- exactly the allowlist, no more and no fewer
- no `rig.ini`, `workspace.ini`, `*.log`, `pointing_model.csv`, `.pdb` or
  capture file
- no file contains `WsToken=` with a value, or the rig's site. The site is
  read from the local `rig.ini` at package time and never written anywhere
- every DLL the exes name is either a Windows system DLL or in the folder,
  checked with `dumpbin /dependents`, so a missing runtime fails here and
  not on someone else's machine
- `Version.h`, the tag and the file names agree

Each check is run once against a planted failure first, a staged
`rig.ini` above all.

**`tests/paths_resolution.cpp`** gains the fresh-install route: `DataRoot`
set by the installer wins, and a data folder on another drive resolves.

**The existing gate** covers 107.1's source changes, with `fits_format`
updated for the new `INSTRUME` and `TELESCOP` behaviour.

---

## 5. Measured, not argued

A clean machine is the only test that counts, and this one cannot be it.
**Owed to the owner, on Elminster or a VM** with no `D:\RadukCapture`,
no Visual Studio and no ZWO software:

1. Run the installer with no administrator prompt.
2. Start the emulator. Does it reach a window, with a usable default
   layout on that machine's monitors?
3. Record a short SER and a short FITS. Do they land in the chosen data
   folder?
4. Close it, reopen it, and confirm the settings held.
5. Install the same version over itself, and confirm nothing was reset.
6. Uninstall, and confirm the data folder is still there.
7. Unzip the portable build elsewhere and start it.

Windows will warn about an unsigned publisher on first run. That is
expected, and the release notes say so. Code signing is a recurring cost
and is out of this phase.

The real camera needs ZWO's own driver installed, and the installer does
not carry it. The hardware build on this rig is checked once from the
installed location: connect, record, stack.

---

## 6. Decisions the owner has to make

Neither blocks 107.1 to 107.4. Both block publishing in 107.5.

1. **The licence (P5.7).** PolyForm Noncommercial, MPL-2.0 or dual. The
   MPL-2.0 draft, PR #30, was closed unmerged. The comparison counts
   licence inside this same score, so it is worth deciding on purpose
   before the first download exists.
2. **ZWO's DLLs.** `README.md` says they remain ZWO's under their own
   terms, and those terms are not in this repository. Read them, or ask
   ZWO. If bundling is not allowed, the fallback is shipping the emulator
   build plus a page saying where to get the two DLLs. That costs some
   convenience, and it does not stop the release.

---

## 7. Not in this phase

- Code signing.
- An auto-updater.
- CI building the release. `master` has no checks at all, which is worth
  its own phase.
- Bundling AutoStakkert, ASTAP or Ollama. They stay optional, and the app
  already says when one is missing.
- A second camera family.
- A NOTICE review past the bundled files: SQLite, the VC++ runtime, ZWO's
  DLLs and the sources behind `object_knowledge.db`, whose attribution
  terms are checked in 107.2.
