# N.I.N.A. (Nighttime Imaging 'N' Astronomy) — Technical Profile

**Research date: 2026-10-18.** Primary evidence is a full clone of the official repository `https://github.com/isbeorn/nina` taken 2026-10-18 at commit `4742afae0a09c79be772f23fcf163fb5852d2775` (2026-10-17 23:34:56 +0000, "Bump version to 3.3.0.1068"), plus the documentation repo `https://github.com/isbeorn/nina.docs` and the plugin manifest repo `https://github.com/isbeorn/nina.plugin.manifests`. File/line counts below were computed directly on that clone and are reproducible.

**Access limitation:** `nighttime-imaging.eu` (the official website, download page, docs site and plugin gallery) was **unreachable from this research environment** — the egress proxy refused the connection (`connect_rejected`). All website-sourced facts below therefore come either from web-search summaries of that site or from the mirrored repositories. The Bitbucket issue tracker (the project's pre-GitHub tracker) now returns HTTP 404 for direct issue URLs.

---

## Implementation language, runtime, UI framework, repository layout and code size

### Takeaway
N.I.N.A. is a Windows-only C# / **.NET 10** / **WPF** desktop application built as a 21-project Visual Studio solution, using `Microsoft.Extensions.DependencyInjection` for DI, `CommunityToolkit.Mvvm` for MVVM, MEF for plugin/sequencer-entity composition, and a Task/async-await concurrency model throughout. Roughly **437k lines of C#** plus **58k lines of XAML** across the repo (including ~104k lines of test code and a vendored Accord.NET fork).

### Cited Findings
- Repository `isbeorn/nina`, default branch `develop`, language C#, topics `astronomy, astrophotography, csharp, dotnet, wpf`, license MPL-2.0, 248 stars, 118 forks, 18 open issues+PRs, repo size 85,476 KB, created on GitHub 2025-06-04, last push 2026-10-17 — GitHub REST repository metadata for [isbeorn/nina](https://github.com/isbeorn/nina)
- `global.json` pins the SDK to `"version": "10.0.400", "rollForward": "latestPatch"` — repo file `global.json` at HEAD, [isbeorn/nina](https://github.com/isbeorn/nina/blob/develop/global.json)
- Every shipping project targets `net10.0-windows` with `<UseWPF>true</UseWPF>`: NINA, NINA.Core, NINA.Equipment, NINA.Image, NINA.Sequencer, NINA.WPF.Base, NINA.Astrometry, NINA.PlateSolving, NINA.Plugin, NINA.Profile, NINA.CustomControlLibrary, NINA.Test, NINA.Benchmark. `NINA.MGEN`, `nikoncswrapper` and `NINA.Sequencer.Generators` target `netstandard2.0`. `NINA/NINA.csproj` is `<OutputType>WinExe</OutputType>` — read from the `.csproj` files at HEAD, [isbeorn/nina](https://github.com/isbeorn/nina)
- The 3.3 changelog's first "General" bullet: "The application now runs on .NET 10, bringing performance improvements and access to the latest runtime features." — `RELEASE_NOTES.md` at HEAD, [isbeorn/nina](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- `NINA.sln` contains **21 project entries** (19 real projects + 2 solution folders "Solution Items" and "3rd Party"): NINA, NINA.CustomControlLibrary, nikoncswrapper, NINA.Setup (`.wixproj`), NINA.SetupBundle (`.wixproj`), NINA.MGEN, NINA.Core, NINA.Equipment, NINA.Profile, NINA.Astrometry, NINA.PlateSolving, NINA.Sequencer, NINA.Image, NINA.WPF.Base, NINA.Plugin, `Accord.Imaging (NETStandard)`, NINA.Test, NINA.Sequencer.Generators, NINA.Benchmark — parsed from `NINA.sln`, [isbeorn/nina](https://github.com/isbeorn/nina/blob/develop/NINA.sln)
- Code size measured on the clone (excluding `.git`): **2,316 `.cs` files / 436,507 lines**; **196 `.xaml` files / 58,210 lines**. Per project (cs+xaml lines): NINA.Test 103,679 · NINA.Equipment 90,726 · Accord.Imaging (vendored fork) 81,369 · NINA 60,530 · NINA.Sequencer 54,275 · NINA.WPF.Base 30,797 · NINA.Core 16,283 · NINA.Image 11,165 · NINA.Profile 10,155 · NINA.Astrometry 8,410 · NINA.MGEN 5,077 · NINA.CustomControlLibrary 2,993 · NINA.PlateSolving 2,830 · NINA.Plugin 2,625 · NINA.Benchmark 1,905 — computed with `find | xargs cat | wc -l` on the clone at HEAD
- Startup/composition chain is documented in-repo: `NINA/App.xaml.cs` → `NINA/CompositionRoot.cs` (builds the service provider, eagerly resolves major view models) → `NINA/Utility/IoCBindings.cs` ("registers the DI graph with `Microsoft.Extensions.DependencyInjection`") → `NINA/MainWindow.xaml` — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- `IoCBindings.cs` contains **99** `AddSingleton` registrations — grep on the clone at HEAD
- MVVM: `CommunityToolkit.Mvvm` is referenced by NINA.Core, NINA.Profile and NINA. The base notify class is `public abstract class BaseINPC : ObservableObject` in `NINA.Core/Utility/BaseINPC.cs`. The repo's agent guidance mandates: "Use `CommunityToolkit.Mvvm` source generators for new or refactored MVVM code whenever they can express the required behavior. Use `[ObservableProperty]`, `[RelayCommand]`…" — [AGENTS.md](https://github.com/isbeorn/nina/blob/develop/AGENTS.md)
- Notable dependency set (from `PackageReference` across all `.csproj`): `CommunityToolkit.Mvvm`, `Microsoft.Extensions.DependencyInjection`, `System.ComponentModel.Composition` (MEF), `Newtonsoft.Json`, `NJsonSchema`, `Serilog` (+File/Console sinks), `OxyPlot.Wpf`, `Dirkster.AvalonDock`, `Microsoft.Xaml.Behaviors.Wpf`, `DotNetProjects.Extended.Wpf.Toolkit`, `MdXaml`, `Microsoft.Web.WebView2`, `System.Data.SQLite` + `System.Data.SQLite.EF6`, `Nito.AsyncEx` / `Nito.Mvvm.Async`, `Accord` / `Accord.Math` / `Accord.Statistics`, `K4os.Compression.LZ4`, `ZstdSharp.Port`, `Iconic.Zlib.Netstandard`, `Grpc.Core.Api` + `Google.Protobuf` + `GrpcDotNetNamedPipes`, `Parlot` and `NCalcSync` (expression parsing), `SharpGIS.NmeaParser`, `Fastenshtein`, `ASCOM.Alpaca.Components` 3.1.0, `ASCOM.Alpaca.Device` 3.1.0, `ASCOM.Com.Components` 3.1.0; tests use NUnit + Moq + FluentAssertions; `NINA.Benchmark` uses BenchmarkDotNet — grep of `.csproj` files at HEAD
- Async model: the public equipment contracts are Task-based, e.g. `Task<IExposureData> DownloadLiveView(CancellationToken token)` in `NINA.Equipment/Interfaces/ICamera.cs`; `NINA.Sequencer`'s runtime entry point `Sequencer.Start(...)` is async and validation/cancellation is threaded through `CancellationToken` — repo source at HEAD, and [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Persistence: EF6 + SQLite — "The EF6/SQLite context is `NINA.Core.Database.NINADbContext`. Its runtime SQL lives under `NINA/Database`" — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- Installer: WiX — `NINA.Setup` is a WiX MSI (`Product.wxs`) and `NINA.SetupBundle` is a WiX Burn bootstrapper — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- `NINA.Test` and `NINA.Benchmark` set `<PlatformTarget>x64</PlatformTarget>`; the test architecture doc notes "x64/native dependencies" — repo `.csproj` files and [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- A Roslyn source generator project, `NINA.Sequencer.Generators`, produces the "expression generator and diagnostics" consumed by sequencer entities and plugin authors — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- Localization goes through `NINA.Core.Locale.Loc`; only `NINA.Core/Locale/Locale.resx` is hand-edited and translated `Locale.<culture>.resx` files are managed by **Crowdin** (`crowdin.yml` present at repo root) — [AGENTS.md](https://github.com/isbeorn/nina/blob/develop/AGENTS.md)
- CI legacy: `bitbucket-pipelines.yml` is still present at the repo root alongside GitHub Actions commits (130 commits authored by "GitHub Actions") — clone at HEAD

### Inferences
- The app is effectively **64-bit-only in practice**: native vendor camera SDKs are x64, tests and benchmarks pin `x64`, and the WiX bundle ships a single MSI. I did not find an explicit `<PlatformTarget>` in `NINA/NINA.csproj`, so the main EXE is likely AnyCPU-with-64-bit-preferred rather than hard-pinned — *inferred, not directly verified*.
- The presence of `bitbucket-pipelines.yml` in both `nina` and `nina.docs`, plus the Bitbucket 404s, indicates the project migrated from Bitbucket to GitHub in mid-2025 and carried its full history (first commit 2016-06-08) across — *inferred from repo artifacts*.
- Vendoring a fork of `Accord.Imaging` (81k lines, `NETStandard` csproj with legacy `TargetFrameworkVersion` leftovers) suggests the upstream Accord.NET project is dead and N.I.N.A. maintains its own copy for star detection/image math — *inferred*.

### Gaps
- Exact binary/installed footprint on disk and the shipped `PlatformTarget` of `NINA.exe` could not be verified (download page unreachable).
- Minimum Windows version / stated system requirements are in `nina.docs/docs/requirements.md` but I did not extract the text; the rendered docs site was unreachable.

---

## Current released version as of October 2026, release date and cadence

### Takeaway
The current **stable** release is **3.2**, released **2025-11-12** — i.e. roughly 11 months old as of October 2026. Version **3.3 is in nightly** (`3.3.0.1068`, assembly version bumped 2026-10-17) and had not shipped as a stable release at the time of research. Cadence over the 3.x line has been roughly one feature release every 8–20 months with hotfixes in between.

### Cited Findings
- `CommonAssemblyInfo.cs` at HEAD: `[assembly: AssemblyVersion("3.3.0.1068")]`, `[assembly: AssemblyFileVersion("3.3.0.1068")]`, `[assembly: AssemblyInformationalVersion("3.3.0.1068-nightly")]` — [isbeorn/nina](https://github.com/isbeorn/nina/blob/develop/CommonAssemblyInfo.cs)
- `RELEASE_NOTES.md` heading order at HEAD: `# Version 3.3 NIGHTLY`, then `# Version 3.2`, `# Version 3.1 Hotfix 2`, `# Version 3.1 Hotfix 1`, `# Version 3.1`, `# Version 3.0`, `# Version 2.3 Hotfix 2`, … — [isbeorn/nina](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- The 3.3 changelog carries an explicit nightly warning: "Nightly builds are preview versions containing active development work. They may be unstable and change frequently." — [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- Git tag dates in the repository: `Version-2.0` 2022-06-05, `Version-2.0.1` 2022-08-28, `Version-2.0.2` 2022-10-13, `Version-2.1` 2022-11-15, `Version-2.2` 2023-03-26, `Version-2.3.1` 2023-10-01, `Version-3.0` 2024-03-18, `Version-3.2` 2025-11-12 — `git log -1` per tag on the clone, [isbeorn/nina](https://github.com/isbeorn/nina/tags)
- A post on the official site dated **2025-11-12** titled "N.I.N.A. Version 3.2 is here" announces 3.2; headline items reported were a refined star detector that calculates centroids for the annotator, and plugins loading in separate assembly contexts to avoid dependency clashes — [nighttime-imaging.eu](https://nighttime-imaging.eu/2025/11/12/n-i-n-a-version-3-2-is-here/) *(URL from search results; page itself was not reachable from this environment)*
- Issue reporters in October 2026 were running **"NINA 3.2.0.9001"**, corroborating 3.2 as the then-current stable build — [issue #416](https://github.com/isbeorn/nina/issues/416), [issue #417](https://github.com/isbeorn/nina/issues/417)
- GitHub repository metadata shows **no GitHub Releases** exposed for the repo (the API `releases` call was not accessible in this session, and the README points downloads to the project website rather than GitHub) — [README.md](https://github.com/isbeorn/nina/blob/develop/README.md)
- `AssemblyMetadata("PluginMinimumApplicationVersion", "3.0.0.0")` — plugins declaring compatibility must target at least 3.0.0.0 — [CommonAssemblyInfo.cs](https://github.com/isbeorn/nina/blob/develop/CommonAssemblyInfo.cs)

### What is in 3.3 (nightly) — headline items from RELEASE_NOTES.md
- **.NET 10 migration** (the first General bullet).
- **Equipment hubs**: combine multiple Safety Monitor, Switch or Weather devices behind a single N.I.N.A. "hub" driver; the weather hub lets you pick which physical device supplies each reading; the safety hub requires every device connected and safe. Code confirms `SafetyMonitorHub.cs`, `CompositeSwitchHub.cs`, `WeatherDataHub.cs`.
- **Advanced Sequencer Undo/Redo**: "keeps the last 100 configuration edits in memory with Undo, Redo and an Edit History sidebar tab showing affected items, their locations and before/after values… Standard plugin fields participate automatically; custom plugin editors can opt in through the editor history context."
- Autofocus-after-HFR-increase gains a "Trend per Filter" checkbox; 160 additional manual-focus target stars; filter wheels poll position in the background; mount dither minimum-movement enforcement; PHD2 guide-star mass/SNR in guide graphs; safety-monitor-aware suppression of imaging triggers (meridian trigger stops tracking instead of flipping when unsafe).
- A large bugfix list including missing synchronization in **native QHY, ToupTek-family, FTDI, SBIG and Moravian SDK calls** "to prevent overlapping operations and premature handle release", and QHY read-mode/gain/offset ordering fixes.
— all from [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md) at HEAD

### Inferences
- Cadence: 2.0 (2022-06) → 2.1 (2022-11) → 2.2 (2023-03) → 2.3.1 (2023-10) → 3.0 (2024-03) → 3.1 (no tag; between 3.0 and 3.2) → 3.2 (2025-11). The gap has been widening; 3.3 had been in nightly for ~11 months by 2026-10 with no stable date announced — *inferred from tag dates and the absence of a 3.3 heading other than NIGHTLY*.
- The fact that 3.3 bundles both a runtime migration (.NET 10) and a large sequencer feature (undo/redo) is consistent with the long nightly period — *inference*.

### Gaps
- No tag exists for `Version-3.1` or for any 3.2 hotfix in the Git repo, so the exact 3.1 release date and whether 3.2 hotfixes shipped could not be established from the repository. The download page would answer this but was unreachable.
- Whether 3.3 stable shipped between 2026-10-17 and the reader's date is unknown.

---

## Licensing, maintainer, contributors, commit frequency

### Takeaway
MPL-2.0, free of charge, fully open source, overwhelmingly a **single-maintainer project**: Stefan Berg ("Isbeorn") authored ~6,360 of 8,152 commits (78%). 78 distinct commit author e-mails over the project's lifetime; 518 commits in the last 12 months.

### Cited Findings
- License: **Mozilla Public License 2.0** (`spdx_id: MPL-2.0`), with `LICENSE.txt` and `COPYING` at the repo root and an MPL-2.0 header block in every source file — GitHub repo metadata and source headers, [isbeorn/nina](https://github.com/isbeorn/nina)
- Source file headers read `Copyright © 2016 - 2026 Stefan Berg <isbeorn86+NINA@googlemail.com> and the N.I.N.A. contributors` — e.g. [NINA.Plugin/Interfaces/IPluginManifest.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Plugin/Interfaces/IPluginManifest.cs)
- `AUTHORS` states the project "was created in 2016 by Stefan Berg" and lists ~37 named contributors (Alister Milne, Dale Ghent, Johannes Josefsen, Stanley Dimant, Yannick Dutertre, Francesco Agosti, Liam Murphy, Omar Martinez, George Hilios, Steve Smith, Aaron Tragle, Ashley Hauck, Maple Gao 高博, Patrick Dufour, Jonathan MacCollum, Chris Turchin, Niccolò Coli, Igor von Nyssen, Simon Kapadia, Francesco Meschia, Nick Hardy, Szymon Giermakowski, …) — [AUTHORS](https://github.com/isbeorn/nina/blob/develop/AUTHORS)
- Git history statistics on the full clone: **8,152 commits**, first commit **2016-06-08** ("Isbeorn, init"), **78 unique author e-mail addresses**, **518 commits in the 12 months to 2026-10-18** — `git rev-list`/`git log` on the clone
- Top commit authors (names as recorded): Stefan Berg 3,806 · Isbeorn 1,706 · Stefan B 845 · darkarchon 328 · Dale Ghent 258 · George Hilios 214 · Yannick Dutertre 179 · Igor von Nyssen 153 · GitHub Actions 130 · "Berg, Stefan" 110 · Marc Blank 58 · Nick Hardy 46 · rootdarkarchon 30 · "Stanley Dimant (darkarchon)" 26 · Francesco Meschia 25 — `git shortlog`-equivalent on the clone
- Monthly commit counts: 2025-08 = 33, 2025-09 = 53, 2025-10 = 78, 2025-11 = 39, 2025-12 = 17, 2026-01 = 14, 2026-02 = 69, 2026-03 = 49, 2026-04 = 81, 2026-05 = 55, 2026-06 = 5, 2026-07 = 11, 2026-08 = 38, 2026-09 = 31, 2026-10 (to the 17th) = 66 — `git log --since` on the clone
- Funding: README badges link to Patreon (`patreon.com/stefanberg`) and the changelog header solicits donations at `nighttime-imaging.eu/donate/` — [README.md](https://github.com/isbeorn/nina/blob/develop/README.md), [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- Community support is on Discord (`discord.gg/nighttime-imaging`), not GitHub Discussions (the repo has `has_discussions: false`) — [README.md](https://github.com/isbeorn/nina/blob/develop/README.md) and GitHub repo metadata

### Inferences
- Combining `Stefan Berg` + `Isbeorn` + `Stefan B` + `Berg, Stefan` gives **6,467 commits ≈ 79%** of all history attributable to the founder. This is a strong bus-factor-of-one signal — *inferred arithmetic on the git log*.
- The 78 "unique authors" figure counts e-mail addresses, so the true human contributor count is lower (several authors appear under 2–4 identities) — *inference*.
- The June–July 2026 trough (5 and 11 commits) coincides with northern-hemisphere short nights; the pattern of summer lulls repeats — *inference*.

### Gaps
- Patreon income, number of patrons, and whether anyone works on N.I.N.A. full-time are unknown (site unreachable).

---

## Hardware abstraction strategy and device class coverage

### Takeaway
Three-layer strategy: **ASCOM COM** drivers, **ASCOM Alpaca** (native, "Alpaca Direct" classes using the official `ASCOM.Alpaca.*` 3.1.0 NuGet packages), and **native vendor SDKs** wrapped via P/Invoke. **There is no INDI support** — a strict grep for `\bINDI\b` across `NINA.Equipment` returns zero hits. All eleven standard ASCOM device classes are covered.

### Cited Findings
- Architecture doc: "`NINA.Equipment`: device abstractions and ASCOM/Alpaca/native adapters" — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- Device class folders under `NINA.Equipment/Equipment/`: `MyCamera`, `MyDome`, `MyFilterWheel`, `MyFlatDevice`, `MyFocuser`, `MyGPS`, `MyGuider`, `MyPlanetarium`, `MyRotator`, `MySafetyMonitor`, `MySwitch`, `MyTelescope`, `MyWeatherData`, plus `Hubs` — clone at HEAD
- **Camera adapters**: `AscomCamera.cs`, `AlpacaDirectCamera.cs`, `ASICamera.cs` (ZWO), `AtikCamera.cs`, `EDCamera.cs` (Canon EDSDK), `FLICamera.cs`, `MoravianCamera.cs`, `NikonCamera.cs`, `QHYCamera.cs`, `SBIGCamera.cs` (+ `SBIGCameraASCOMService.cs`), `SVBonyCamera.cs`, `ToupTekAlikeCamera.cs`, `GenericCamera.cs`, `FileCamera.cs`, `PersistSettingsCameraDecorator.cs` — clone at HEAD
- **Native camera SDK wrappers** under `NINA.Equipment/SDK/CameraSDKs/`: `ASISDK`, `ASTPANSDK`, `AtikSDK`, `EDSDK`, `FLISDK`, `MoravianSDK`, `PlayerOneSDK`, `QHYSDK`, `SBIGSDK`, `SVBonySDK`, `ToupTekAlike` (which itself contains `altaircam.cs`, `nncam.cs` (Risingcam), `svbonycam.cs`, `ogmacam.cs`, `toupcam.cs`, `mallincam.cs`) — clone at HEAD
- Other native SDKs: `FocuserSDKs/OasisSDK`, `FilterWheelSDKs/OasisSDK`, `FlatDeviceSDKs/{AllPro, AlnitakSDK, Artesky, PegasusAstroSDK}` — clone at HEAD
- **Focuser**: `AscomFocuser`, `AlpacaDirectFocuser`, `ASIFocuser` (ZWO EAF native, new in 3.2), `OasisFocuser`, `ToupTekAlikeFocuser` — clone at HEAD; the ZWO EAF native driver is listed under 3.2 "Device Management" in [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- **Filter wheel**: `AscomFilterWheel`, `AlpacaDirectFilterWheel`, `ASIFilterWheel`, `AtikFilterWheel`/`AtikInternalFilterWheel`, `FLIFilterWheel`, `ManualFilterWheel`, `MoravianIntegratedFilterWheel`, `OasisFilterWheel`, `PlayerOneFilterWheel`, `QHYFilterWheel`, `SBIGFilterWheel`, `ToupTekAlikeFilterWheel` — clone at HEAD
- **Rotator**: `AscomRotator`, `AlpacaDirectRotator`, `ManualRotator`. **Dome**: `AscomDome`, `AlpacaDirectDome`, plus `DomeSynchronization.cs`. **Safety monitor**: `AscomSafetyMonitor`, `AlpacaDirectSafetyMonitor`, `SafetyMonitorHub`. **Switch**: `AscomSwitch` (folder `Ascom`), `AlpacaDirectSwitch`, `CompositeSwitchHub`, `DummySwitch`. **Telescope**: `AscomTelescope`, `AlpacaDirectTelescope` — clone at HEAD
- **Flat panel**: `AscomCoverCalibrator`, `AlpacaDirectCoverCalibrator`, `AllProSpikeAFlat`, `AlnitakFlatDevice` (+ simulator), `ArteskyFlatBox`, `PegasusAstroFlatMaster` — clone at HEAD
- **Weather**: `AscomObservingConditions`, `AlpacaDirectObservingConditions`, plus internet sources `OpenMeteo`, `OpenWeatherMap`, `TheWeatherCompany`, `WeatherUnderground`, and `WeatherDataHub` — clone at HEAD
- **GPS**: `NMEAGps`, `Gpsd`, `PrimaLuceLabEagle`, `PegasusAstro` — clone at HEAD
- **Planetarium integrations** (for target import): `C2A`, `CartesDuCiel`, `HNSKY`, `SkytechX`, `Stellarium`, `TheSkyX` — clone at HEAD
- ASCOM package versions: `ASCOM.Alpaca.Components` 3.1.0, `ASCOM.Alpaca.Device` 3.1.0, `ASCOM.Com.Components` 3.1.0, `ASCOM.Tools` 3.1.0 (test project) — [NINA.Equipment.csproj](https://github.com/isbeorn/nina/blob/develop/NINA.Equipment/NINA.Equipment.csproj)
- 23 files under `NINA.Equipment` reference Alpaca — grep on the clone
- Strict grep for `\bINDI\b` in `NINA.Equipment/**/*.cs` → **0 matches**; no INDI client library in any `PackageReference` — grep on the clone at HEAD
- Open issue #144 "I would like to have Linux/MacOS support" is labelled **wontfix** with 9 reactions — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue+is%3Aopen+sort%3Areactions-%2B1-desc)

### Inferences
- The absence of INDI plus the `wontfix` on Linux/macOS means N.I.N.A. is a deliberate Windows/ASCOM-ecosystem product. A third-party Linux fork, "PI.N.S. (PI 'N' Stars)", exists based on the N.I.N.A. codebase, but I could not verify its relationship or activity — *weak, from a search summary of [github.com/MikeS11/pins](https://github.com/MikeS11/pins)*.
- The "Equipment hubs" feature in 3.3 is a software multiplexer over ASCOM device classes, not a new transport — *inferred from RELEASE_NOTES.md plus the Hub class names*.

### Gaps
- Number of distinct camera models supported (as opposed to SDK families) is not published in-repo.

---

## Plugin architecture

### Takeaway
Plugins are **.NET class libraries** compiled against N.I.N.A.'s public assemblies, discovered at runtime by **MEF** (`System.ComponentModel.Composition`) from versioned folders under `%LOCALAPPDATA%\NINA`, described by a JSON manifest validated against a published JSON schema, and distributed through a **separate Git repository of manifests** (`isbeorn/nina.plugin.manifests`) rather than NuGet. **109 distinct plugins** were in the manifest repository as of 2026-10-18.

### Cited Findings
- "`NINA.Plugin.PluginLoader` loads built-in sequencer/entity types first, scans plugin DLLs from versioned folders under `%LOCALAPPDATA%\NINA` and composes items, conditions, triggers, containers, dockable view models, pluggable behaviors and equipment providers through **MEF**. It also merges plugin resource dictionaries into the WPF application resources." — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- The extension points a plugin can contribute are therefore: **sequence items, conditions, triggers, containers, dockable view models, pluggable behaviors (e.g. autofocus via `IAutoFocusVMFactory` — see `NINA/Utility/PluggableAutoFocusVMFactory.cs`), equipment providers, and WPF resource dictionaries** — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md) and clone at HEAD
- `NINA.Plugin` is a small project (25 `.cs` files, 2,625 lines) consisting of `PluginLoader.cs`, `PluginInstaller.cs`, `PluginFetcher.cs`, `PluginCompatibility.cs`, `PluginAssemblyReader.cs`, `PluginMergedEntityMap.cs`, `PluginBase.cs`, `Constants.cs`, `Messaging/MessageBroker.cs`, a `ManifestDefinition/` folder (`PluginManifest`, `PluginVersion`, `PluginDescription`, `PluginInstallerDetails`, `InstallerChecksum`, `InstallerType`, `ConcreteManifestConverter`) and `Interfaces/` (`IPluginManifest`, `IPluginVersion`, `IPluginDescription`, `IPluginInstallerDetails`, `IPluginLoader`, `IMessageBroker`, `IMessage`, `ISubscriber`) — clone at HEAD
- `IPluginManifest` surface: `Identifier`, `Name`, `License`, `LicenseURL`, `Author`, `Homepage`, `Repository`, `ChangelogURL`, `Tags[]`, `Version`, `MinimumApplicationVersion`, `Installer`, `Descriptions`, plus lifecycle `Task Initialize()` and `Task Teardown()` — [IPluginManifest.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Plugin/Interfaces/IPluginManifest.cs)
- Plugin↔plugin and plugin↔core decoupled messaging exists via `IMessageBroker` / `IMessage` / `ISubscriber` (`NINA.Plugin/Messaging/MessageBroker.cs`) — clone at HEAD
- Versioning/compatibility: `AssemblyMetadata("PluginMinimumApplicationVersion", "3.0.0.0")` in `CommonAssemblyInfo.cs` is the floor a plugin must declare; `PluginCompatibility.cs` enforces it at load — [CommonAssemblyInfo.cs](https://github.com/isbeorn/nina/blob/develop/CommonAssemblyInfo.cs)
- Distribution: `isbeorn/nina.plugin.manifests` contains `manifest.schema.json`, `gather.js`, `validate-all-manifests.js`, `validate-latest-manifest.js`, and a `manifests/<first-letter>/<Plugin Name>/<version>.json` tree with **432 JSON files across 109 distinct plugin directories**; most recent commit 2026-10-18 13:10 "Web Viewer 2.3.0.0 (#745)" — clone of [isbeorn/nina.plugin.manifests](https://github.com/isbeorn/nina.plugin.manifests)
- Full plugin list (109, as of 2026-10-18): 10 Micron Tools; 2-Point Polar Alignment; ADPUK_AddToCPWIAlignmentModel; ARIS; ASA Tools; ASG Electronically Assisted Tilt; Alpaca; Astraeus; Astral Warden Beacon; Astro Coverage Planner (ACP); Astro PM; Astro-Physics Tools; AstroAppHTTPAPI; AstroColibri; AstroManager; Astromechanics Aperture Control; Astrovault; AutofocusReportAnalysis; BahtiFocus; Benchmark; CGPUNINA; Chatstronomy; Click to Center; CollimationHelperForSkyWave; Connector; DeepSkyLog; Device Actions and Commands; Discord Alert; DiscordNotification; Dither Statistics; DynamicSequencer; ExoPlanets; Exposure Calculator; FasterFlats; Filter Offset Calculator; Flat Epoch; Flexure Compensator; GNS plugin; Ground Station; GuidingAnalyzer; **Hocus Focus**; Home Assistant; Horizon Studio; HorizonCreator; InfluxDB Exporter; InjectAutofocus; Instruction Math; Lens AF; Lightbucket; **Livestack**; Log Viewer; **LuckyImaging**; Lumix; Manual Focuser; ManualRotatorOAG; Maximum Horizon; Moon Angle; NEOCPHelper; Night Summary; NightSage; NikonEkrynoxSdk; OnStepXTools; Orbitals; Orbuculum; PHD2 Tools; Pentax Camera; Pentax Vintage Camera; Perihelion; PixInsight Tools; PlaneWave Tools; PlateSolvePlus; Point3D; Prometheus Exporter; Python Scripting; QualitySessionMeter; Quick Actions; RTSP Client; RTSP Timelapse Control; Remote Copy; SGP Server Emulation; ScopeControl; SeqNr; SequencerPlus; Session MetaData; Shutdown PC; Sites; Skyflats; Smart Filters; Smart Plug Control; SmartCalFrames; Solve Every Light; Sony Camera; SpeckleInterferometry; Star Sentinel; StarMessenger; **Subframes**; Synchronization; **Target Scheduler**; Target Planning; TenMicron; **Three Point Polar Alignment**; **Tonight's Best**; **Touch-N-Stars**; WandererNinaPlugin; Web Session History Viewer; aiassistant; aiweather; **ninaAPI**; opencode — directory listing of [isbeorn/nina.plugin.manifests](https://github.com/isbeorn/nina.plugin.manifests/tree/main/manifests)
- 3.2 changed plugin isolation: "plugins now load in separate assembly contexts to avoid dependency clashes" — [nighttime-imaging.eu 3.2 announcement](https://nighttime-imaging.eu/2025/11/12/n-i-n-a-version-3-2-is-here/) *(from search summary; page not directly reachable)*
- Plugin-developer API complaints are catalogued in open issue **#26 "Plugin developers wishlist"** (opened 2025-07-15 by JohannesWorks, pinned 2025-07-20, labelled `enhancement`, 9 reactions). Requests include: loading sequences from files into the Advanced Sequencer; **sequencer execution events** (e.g. an event when a sequence item runs); a **plate-solve-failed event**; **sequence root container access via the sequence mediator**; **stable IDs/GUIDs for `SequenceEntity` items** so an item can be found after its values change; the ability to **stop a running autofocus**; and **build flexibility for targeting multiple N.I.N.A. versions**. One developer said his image-handling workaround used **reflection** and he would prefer a supported API. Maintainer `isbeorn` pointed at the existing `ImagingMediator.ImagePrepared` event (2025-07-29) but the thread shows no commitment to the remaining items — [issue #26](https://github.com/isbeorn/nina/issues/26)

### Inferences
- The manifest repo's `745`-numbered PR for a single plugin version bump implies the plugin ecosystem is actively curated via pull request, with roughly 745 manifest submissions since the repo began — *inferred from the PR number in the last commit subject; the absolute count is not confirmed*.
- Because plugins are MEF-composed DLLs compiled against N.I.N.A.'s own assemblies (not a versioned NuGet contract surface for most types), a plugin ABI break accompanies major N.I.N.A. versions; `MinimumApplicationVersion` is the only declared guard. The #26 request for "build flexibility for targeting different NINA versions" supports this reading — *inference from the manifest interface plus [issue #26](https://github.com/isbeorn/nina/issues/26)*.
- `ninaAPI` (community REST/WebSocket API plugin) and `Touch-N-Stars` (mobile web UI) are the de-facto remote-control story, since there is no first-party HTTP API in the core — *inference from the plugin list; not verified against those plugins' docs*.

### Gaps
- Per-plugin download/popularity counts are published on the website's plugin gallery, which was unreachable. I cannot rank plugins by usage; the bolded entries above are flagged by my own domain judgement, not by a cited popularity metric.
- There is a plugin **template/SDK repository** referenced by the community (a `dotnet new` template), but I could not verify its URL or contents from this environment.

---

## The Advanced Sequencer: data model, composition, persistence, learning curve

### Takeaway
The Advanced Sequencer is a **runtime object tree** (not a declarative document): containers nest items, conditions and triggers; entities are discovered by MEF and reconstructed on load through a factory; and sequences/templates/targets persist as **JSON with `TypeNameHandling.All` and `PreserveReferencesHandling.All`** (templates use the `.template.json` extension). Practitioners consistently describe it as powerful but initially intimidating.

### Cited Findings
- Structure of `NINA.Sequencer` (344 files, 54,275 lines): `SequenceItem/` grouped by feature area (`Autofocus`, `Camera`, `FilterWheel`, `Focuser`, `Guider`, `Imaging`, `Platesolving`, `Rotator`, `SafetyMonitor`, `Switch`, `Telescope`, `Utility`, `Dome`, `Connect`, `Expressions`); `Container/` (`SequenceRootContainer`, `SequentialContainer`, `ParallelContainer`, `TargetAreaContainer`, `StartAreaContainer`, `EndAreaContainer`); `Conditions/`; `Trigger/`; `Serialization/`; `Logic/` (`Expression`, `SymbolBroker`, `UserSymbol`); `View/` — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Execution model: "`Sequencer.Start(...)` validates the root container, initializes every item/condition/trigger, runs the root container, and then tears the tree down again. Validation is recursive and based on the `IValidatable` interface. **Containers, conditions, and triggers are normal runtime objects, not pure data nodes.**" — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Entity discovery: "built-in sequence entities are exported with MEF metadata (`[Export]`, `[ExportMetadata]`); `NINA.Plugin.PluginLoader` loads both built-in and plugin-provided entities; `SequencerFactory` receives the final entity lists and exposes cloneable prototypes" — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Serialization: `Serialization/SequenceJsonConverter.cs` "serializes with `TypeNameHandling.All` and `PreserveReferencesHandling.All`" and "deserializes through creation converters backed by `ISequencerFactory`" — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Load-path optimisation (recent): "File loaders use `SequenceJsonConverter.DeserializeFromFile` to avoid retaining a complete UTF-16 copy of the input. For ordinary files, a bounded preflight validates the document and identifies canonical built-in root and target-area containers before any models are created. A second pass … populates those containers incrementally… **The sequence model is still constructed eagerly; UI virtualization only bounds the realized controls.**" — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Templates persist as `public const string TemplateFileExtension = ".template.json";` in `NINA.Sequencer/TemplateController.cs`, loaded from a user templates folder with a `FileSystemWatcher` and `SearchOption.AllDirectories`; targets are handled by a parallel `TargetController` with its own `TargetsFileExtension` — clone at HEAD
- Expressions: `NINA.Sequencer/Logic/` provides `Expression`, `SymbolBroker`, `UserSymbol`; parsing uses `Parlot` and `NCalcSync`; the `NINA.Sequencer.Generators` Roslyn generator emits "generated scalar properties" for expression-backed entities with "plugin-author diagnostics" — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md), [AGENTS.md](https://github.com/isbeorn/nina/blob/develop/AGENTS.md), `.csproj` package list
- Expressions are documented as a **3.3** feature: the docs nav entry reads `Expressions (NINA 3.3): 'sequencer/advanced/expressions.md'` — [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml)
- 3.3 adds Undo/Redo: "keeps the last 100 configuration edits in memory with Undo, Redo and an Edit History sidebar tab showing affected items, their locations and before/after values. History remains available during unlocked execution…" — [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- Documentation structure confirms two sequencers coexist — "Legacy Sequencer" (Overview, Sequencing) and "Advanced Sequencer" (Overview, Instructions, Loop Conditions, Triggers, Expressions, From Legacy To Advanced) — [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml)
- Practitioner sentiment (mixed, from forum threads surfaced by search; **I could not open Cloudy Nights threads directly**): a new user called it "a steep learning curve"; IceInSpace users said they had avoided it because it felt "beyond me" and "full-on"; an AAVSO variable-star observer found the interface "space-hungry" and said it "gets very clumbsy after a while", with a multi-target plan failing with no error explanation; a Reddit commenter suggested starting with the simple sequencer if Advanced is "a little intimidating". Counterpoints: an AstroBin user advised skipping the Legacy Sequencer entirely, citing drag-and-drop and community templates; another said it was "intuitive once they started"; an APT convert called it "insanely customizable" and the main reason they switched — summarised from [AstroBin "new to NINA"](https://app.astrobin.com/forum/topic/117188/new-to-nina), [AstroBin "Why should I move from APT to Nina?"](https://app.astrobin.com/forum/topic/41353/why-should-i-move-from-apt-to-nina), [IceInSpace](https://www.iceinspace.com.au/forum/showthread.php?p=1522005), [AAVSO comment](https://mintaka.aavso.org/comment/165307), [r/AskAstrophotography "NINA sequencer"](https://redlib.belloworld.it/r/AskAstrophotography/comments/1da630k/nina_sequencer)

### Inferences
- `TypeNameHandling.All` means the JSON embeds fully-qualified .NET type names including assembly identity. That makes sequence files tightly coupled to the exact set of installed plugins and to assembly naming, which explains the repo's repeated emphasis on "preserve serialized compatibility" and the legacy-assembly fallback logic — *inference from the serialization description in [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)*.
- "The sequence model is still constructed eagerly" is an acknowledged scaling limit: a large sequence materialises every runtime object before the UI virtualizes anything, which is consistent with the memory/UI complaints in issue #417 — *inference linking two cited sources*.

### Gaps
- I did not extract an example sequence JSON to quantify file size per instruction, nor the exact `TargetsFileExtension` string.
- No quantitative data on how many users use Advanced vs. Legacy sequencer.

---

## Feature coverage

### Takeaway
Comprehensive DSO feature set: two autofocus measurement methods with four curve fits, eight plate-solver back-ends, three guider integrations plus direct guiding, framing assistant, sky atlas, flat wizard, automated meridian flip, dithering, and three output formats (FITS, XISF, TIFF) plus RAW passthrough. Polar alignment and live stacking are **plugins, not core features**.

### Cited Findings — autofocus
- Two measurement methods: **Star HFR** (half-flux radius of detected stars) and **Contrast Detection** (Sobel / Laplace / statistics-based; works on the Moon, solar-system or terrestrial objects; "still currently experimental") — [nina.docs advanced/autofocus.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/autofocus.md)
- Four curve fits implemented as separate classes in `NINA.WPF.Base/Utility/AutoFocus/`: `TrendlineFitting.cs`, `HyperbolicFitting.cs`, `QuadraticFitting.cs` (parabolic), `GaussianFitting.cs`, plus `Trendline.cs` and `AutoFocusReport.cs` — clone at HEAD; each has a dedicated NUnit fixture in `NINA.Test/Autofocus/`
- Algorithm: starts with a benchmark exposure, steps inward by *Auto Focus Step Size* until a minimum/maximum is found with at least *Auto Focus Initial Offset Steps* points either side, fits the curve, moves to best focus, then takes a validation exposure — "If the final HFR is worse than the initial HFR by **15% or more**, the autofocus routine is deemed a failure and the focuser returns to its initial focus position, or an additional auto-focus run is attempted." Defaults: Initial Offset Steps 4 for Star HFR, 6 for contrast — [nina.docs advanced/autofocus.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/autofocus.md)
- Autofocus is a **pluggable behavior**: `IAutoFocusVMFactory` / `BuiltInAutoFocusVMFactory` / `NINA/Utility/PluggableAutoFocusVMFactory.cs` — this is the hook the third-party **Hocus Focus** plugin uses — clone at HEAD (the Hocus Focus connection is *inferred*)
- Five autofocus triggers: `AutofocusAfterFilterChange`, `AutofocusAfterHFRIncreaseTrigger`, `AutofocusAfterTemperatureChangeTrigger`, `AutofocusAfterTimeTrigger`, `AutofocusAfterExposures` — `NINA.Sequencer/Trigger/Autofocus/`, clone at HEAD
- Focuser backlash measurement is a documented feature — [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml) (`advanced/backlashmeasurement.md`)

### Cited Findings — plate solving
- Eight solver implementations in `NINA.Platesolving/Solvers/`: `ASTAPSolver.cs`, `AllSkyPlateSolver.cs`, `AstrometryPlateSolver.cs` (Astrometry.Net online API), `LocalPlateSolver.cs` (local Astrometry.Net / ansvr), `Platesolve2Solver.cs`, `Platesolve3Solver.cs`, `Dc3PinPointSolver.cs`, `TheSkyXImageLinkSolver.cs`, with shared `CLISolver.cs`/`BaseSolver.cs` and a `PlateSolverFactory` — clone at HEAD
- **ASTAP is the recommended solver** ("### ASTAP - *recommended*"). PlateSolve3.80 "was kindly selected to include… courtesy of Dave Rowe of PlaneWave Instruments" and "handles longer focal lengths and small FOVs, and solves quickly with few stars (<10)". Failed solves are retained at `%LOCALAPPDATA%\NINA\PlateSolver\Failed` — [nina.docs advanced/platesolving.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/platesolving.md)
- Orchestration classes: `CaptureSolver`, `CenteringSolver`, `ImageSolver`, `PlateSolveResult`, `PlateSolveProgress` — clone at HEAD
- Open enhancement issue **#326 "Design Discussion: New Plate Solvers"** (labelled `enhancement`) — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue+is%3Aopen+sort%3Areactions-%2B1-desc)

### Cited Findings — guiding, dithering, meridian flip
- Guider adapters in `NINA.Equipment/Equipment/MyGuider/`: `PHD2/` (directory), `MGENGuider.cs` (via the standalone `NINA.MGEN` project implementing MGEN2/MGEN3 transport+protocol), `MetaGuideGuider.cs` + `MetaGuide/`, `SkyGuard/`, `DirectGuider.cs` (ST4-less direct pulse guiding through the mount), `DummyGuider.cs`, plus `DitherOffsetSelector.cs`, `LockPosition.cs`, `RMSError.cs`, `GuideStepsHistory.cs` — clone at HEAD
- Docs list guiders as "PHD2, MGEN2, MetaGuide"; "N.I.N.A. can automatically start PHD2 and connect to it"; MetaGuide uses lucky imaging and reports pixel scale to N.I.N.A. for correct dither amounts — [nina.docs advanced/guiding.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/guiding.md)
- SkyGuard support was extended in 3.2: "Added support for latest version of SkyGuard (SkySurveyor, SkyGuard, SkyGuide, SkyWave depending on license)" — [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- Dithering docs distinguish **Standard Dithering** (via the guider) from **Built-in Dithering** (mount-based). 3.3 adds: "Mount Dither can now enforce a configurable minimum movement between exposures" — [nina.docs advanced/dithering.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/dithering.md), [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- Automated meridian flip is a core documented feature with a dedicated trigger area (`NINA.Sequencer/Trigger/MeridianFlip`) and "programmable meridian-flip action sets" in the 3.3 edit-history work — [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml), [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)

### Cited Findings — framing, atlas, flats, polar alignment, live stacking
- Core tabs documented: Sky Atlas, Framing Assistant, Flat Wizard, Sequencer, Imaging, Equipment, Options, Plugins — [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml)
- An offline sky-map pipeline exists (`NINA.WPF.Base/SkySurvey/ARCHITECTURE.md`) and 3.3 fixes "Cached Offline Sky Map and Sky Atlas image tiles now keep a stable orientation across zoom levels" — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md), [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- **Polar alignment is not in core**: there is no polar-alignment page in the documentation nav, and the function is supplied by the plugins **"Three Point Polar Alignment"** and **"2-Point Polar Alignment"** — [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml), [nina.plugin.manifests](https://github.com/isbeorn/nina.plugin.manifests/tree/main/manifests)
- **Live stacking is not in core**: supplied by the **"Livestack"** plugin — [nina.plugin.manifests](https://github.com/isbeorn/nina.plugin.manifests/tree/main/manifests)
- Flat Wizard is core; 3.3 improves trained flat matching and adds sequencer flat instructions (`flat_autoexposure`, `flat_autobrightness`, `flat_trainedflat`, `flat_traineddark`, `flat_sky`, `flat_light`, `flat_open`, `flat_close`, `flat_brightness`) — docs image inventory and [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)

### Cited Findings — image file formats
- `NINA.Core/Enum/FileTypeEnum.cs` enumerates exactly: **`TIFF`, `FITS`, `XISF`, `RAW`**, plus `[Obsolete] TIFF_ZIP` and `[Obsolete] TIFF_LZW` — [FileTypeEnum.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Core/Enum/FileTypeEnum.cs)
- Writer implementations live only in `NINA.Image/FileFormat/FITS/` (incl. `CFitsioFITS.cs`, `FITSHeader.cs`) and `NINA.Image/FileFormat/XISF/` (`XISF.cs`, `XISFHeader.cs`, `XISFImageProperty.cs`); TIFF is written via `System.Drawing`/WPF codecs. There is **no SER and no AVI writer anywhere in the repository** (a repo-wide grep for `AviWriter`, `.avi`, `.ser` matched only a vendored Accord test-images file) — clone at HEAD
- FITS: "can be saved uncompressed or with RICE, PLIO, HCOMPRESS, GZIP1 or GZIP2 compression. Compression is applied through CFITSIO… The optional `.fz` suffix can be added" — [nina.docs advanced/file_formats/fits.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/file_formats/fits.md)
- XISF: compression options LZ4, LZ4HC, Zlib, **Zstandard (ZSTD)**, optional byte shuffling, and optional checksum for data integrity — [nina.docs advanced/file_formats/xisf.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/file_formats/xisf.md)
- TIFF: uncompressed ("normally the fastest option, but also produces the largest files"), ZIP or LZW lossless — [nina.docs advanced/file_formats/tiff.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/file_formats/tiff.md)

### Inferences
- `RAW` in `FileTypeEnum` is DSLR raw passthrough (the repo has `NINA.Image/RawConverter/`), not a N.I.N.A.-authored format — *inference*.
- Because polar alignment, live stacking, lucky imaging and subframing are all delivered by plugins, the core product's scope is deliberately "sequenced long-exposure DSO acquisition", with everything adjacent pushed to the plugin boundary — *inference*.

### Gaps
- I did not verify bit depths written per format (16-bit vs 32-bit float) from the code.

---

## Planetary / high-frame-rate capture

### Takeaway
**N.I.N.A. does not support planetary/high-frame-rate capture in any meaningful sense.** There is no video file writer of any kind (no SER, no AVI), no frame-rate control or FPS metric in the camera abstraction, and "live view" is a one-frame-at-a-time `Task<IExposureData>` polling loop driven from a WPF view model. ROI/subframing exists but is per-exposure, and the maintainer has publicly described N.I.N.A.'s ROI feature as something he "would rather see… removed", pointing users at FireCapture/SharpCap for planetary work.

### Cited Findings
- **No video output.** `FileTypeEnum` is `TIFF | FITS | XISF | RAW` only; a repository-wide grep found no SER or AVI writer — [FileTypeEnum.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Core/Enum/FileTypeEnum.cs) and clone at HEAD
- **Live view API shape**: `ICamera` exposes `bool CanShowLiveView`, `bool LiveViewEnabled`, `void StartLiveView(CaptureSequence sequence)`, `Task<IExposureData> DownloadLiveView(CancellationToken token)`, `void StopLiveView()`. The consumer is `NINA.WPF.Base/ViewModel/Equipment/Camera/CameraVM.cs` line ~682, `var iarr = await _cam.DownloadLiveView(ct);` inside a loop. Implementations exist for ASI, ToupTek-alike, SBIG, Nikon, `GenericCamera` and the simulator — clone at HEAD, [ICamera.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Equipment/Interfaces/ICamera.cs)
- There is **no FPS/frame-rate property, no exposure-queue depth and no ring buffer** in `ICamera`; each live-view frame is a full `IExposureData` object that goes through the normal image pipeline — clone at HEAD
- **ROI/subframe support exists but is exposure-scoped**: `ICamera` has `CanSubSample`, `EnableSubSample`, `SubSampleX/Y/Width/Height`, `UpdateSubSampleArea()`; `CaptureSequence` carries `EnableSubSample` and an `ObservableRectangle SubSambleRectangle` [sic — the typo is in the source]; the snapshot view model exposes `SnapSubSample` — clone at HEAD
- The maintainer's position on ROI, from the (now 404) Bitbucket issue #381 "Subframing or Region of Interest support": ROI "in general is used to increase the frame rate of the camera for planetary imaging, which Firecapture is most geared towards"; long DSO exposures "do not require these high frame rates outside of lucky imaging or other niche uses, which one would use SharpCap or Firecapture for anyhow"; and he calls N.I.N.A.'s ROI feature "so uncompelling that I would rather see the concept removed"; also "with usb3 framerate is a minimal driver of these" — [bitbucket.org/Isbeorn/nina issue 381](https://bitbucket.org/Isbeorn/nina/issues/381/subframing-or-region-of-interest-support). **Caveat: this URL now returns HTTP 404; the quotations come from a search-engine summary of the cached page, not from a page I was able to load. Treat as strongly indicative but not independently verified.**
- Open issue **#419 "allow to use subframes for the slew and center funtion"** (2026-10-07) shows subframing is still being requested even for plate-solving use — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue)
- A **"Subframes"** plugin and a **"LuckyImaging"** plugin exist in the plugin catalogue, indicating the gap is filled (partially) outside core — [nina.plugin.manifests](https://github.com/isbeorn/nina.plugin.manifests/tree/main/manifests)
- Practitioner workflow reported on forums: use N.I.N.A. to plate solve, focus (watching HFR) and sync the mount, **then close N.I.N.A. and switch to FireCapture** to slew to the planet and record a `.ser`/`.avi`; one commenter notes "periodic focusing isn't offered by firecapture", which is why N.I.N.A. is used alongside it — [AstroBin "noob planetary dreaming"](https://app.astrobin.com/forum/topic/154180/noob-planetary-dreaming-nervous-noob-questions)
- Contrast-detection autofocus explicitly "will also work on the Moon, solar system objects, or terrestrial objects" — the one planetary-adjacent core capability — [nina.docs advanced/autofocus.md](https://github.com/isbeorn/nina.docs/blob/master/docs/advanced/autofocus.md)

### Inferences
- **Frame-rate ceiling:** because every live-view frame round-trips through `Task<IExposureData>` → image pipeline → WPF `BitmapSource` on the UI thread, and because there is no buffering layer, the practical ceiling is display-refresh-bound (single-digit to low-tens of FPS) rather than sensor/USB-bound. **I found no published FPS number for N.I.N.A. live view; this is inference from the API shape, not a measurement.**
- N.I.N.A. is architecturally unsuitable for the planetary use case and the project knows it: live view is positioned as a framing/focusing aid, not a capture mode. Any head-to-head with a Win32/C++ planetary capture app is therefore a comparison of non-overlapping products, except for the shared ground of focusing, mount control and plate solving — *inference*.

### Gaps
- No measured FPS figures, ROI frame-rate scaling data, or dropped-frame statistics exist in any source I could reach.
- The Bitbucket issue #381 text could not be loaded directly (404); the maintainer quotations should be re-verified before being published verbatim.

---

## Performance characteristics and known limitations

### Takeaway
Managed-runtime behaviour dominates: multi-GB working sets on long sessions with large sensors, UI-thread contention in the sequencer tree, a deliberately throttled image-save queue, no GPU acceleration anywhere, and no multi-camera support in a single instance. Two October 2026 issues (#416, #417) document concrete numbers on a 62 MP camera.

### Cited Findings
- **Memory / long-session behaviour (#417, 2026-10-06, reporter ARP273-ROSE):** N.I.N.A. **3.2.0.9001** ran **37 hours** without restart on a Ryzen 9 7940HS / 32 GB / NVMe / Windows 11 Pro / Radeon 780M with a **62 MP** camera and reached **13.8 GB private memory (9 GB working set)**, with system commit at **26 of 32 GB**, while using about one full CPU core (6.6% of 16 threads). The sequence had ~**50 containers, ~80 triggers, 23 Smart Exposure loops** across 5 targets. Symptoms: vertical scrolling in the Advanced Sequencer stuttered/froze and the UI became sluggish. Requested fixes: virtualize the sequencer tree, skip rendering collapsed containers, throttle UI updates during trigger evaluation, process previews at display size, check memory retention. Maintainer `isbeorn` replied 2026-10-08 that **PR #424 "should tackle most of this"** and closed the issue as completed the same day — [issue #417](https://github.com/isbeorn/nina/issues/417)
- Reporter workarounds in #417 (useful as a statement of the product's limits): collapsing containers, moving common triggers to global level, setting preview noise reduction to None, turning off annotation, and **restarting N.I.N.A. before each night** — [issue #417](https://github.com/isbeorn/nina/issues/417)
- **Save-path throughput (#416, 2026-10-06 → closed 2026-10-08):** a 62 MP ASI6200MM Pro frame (9576×6388 UInt16, ~122 MB raw) written as XISF with **ZLIB + byte shuffling + SHA-512** took **3.5–4 minutes** to finalize (temp FITS at 22:30:08.5, final XISF at 22:34:01.6) producing ~54 MB; saves queued and a 60 s light effectively took ~2 minutes. The reporter's single-threaded Python zlib-level-6 + shuffle test took ~9 s per frame. Maintainer `isbeorn`: this is **not a bug** — zlib is slow and not recommended, use **LZ4 or Zstandard**; the save queue "deliberately caps pending images to prevent memory backlog" and lifting the cap would not fix throughput. Contributor `daleghent` opposed parallel chunking because low-power systems (4-core Intel N-series) could be overwhelmed, and suggested evaluating SIMD-optimised **zlib-ng**. Parallel compression and a non-blocking save path were **not adopted** — [issue #416](https://github.com/isbeorn/nina/issues/416)
- Closed issue **#418 "Mount command/response lines (TelescopeMediatorMountCommander) logged at INFO: 97 % of a 120 MB log"** (2026-10-06) — log-volume defect — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue)
- Closed issue **#415 "Nested 'Loop until time' restarts the inner set in a tight loop once the next instruction no longer fits (3.9M log lines in under 3 minutes)"** (2026-10-08) — sequencer runaway — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue)
- Architecture doc concedes the eager-construction limit: "**The sequence model is still constructed eagerly; UI virtualization only bounds the realized controls.**" — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- Load-path memory work: "`DeserializeFromFile` to avoid retaining a complete UTF-16 copy of the input"; "Creation converters retain a **bounded cache** of successfully resolved built-in sequencer type names" — [NINA.Sequencer/ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/NINA.Sequencer/ARCHITECTURE.md)
- **No GPU compute**: a repo-wide grep for `CUDA|OpenCL|cuFFT|ILGPU|ComputeSharp` in `.cs` files returns **zero** matches, and no GPU library appears in any `PackageReference`. Rendering is plain WPF/Direct3D-9-backed; the docs carry a dedicated troubleshooting page `troubleshooting/render_issues.md` — clone at HEAD, [nina.docs mkdocs.yml](https://github.com/isbeorn/nina.docs/blob/master/mkdocs.yml)
- A `NINA.Benchmark` project (BenchmarkDotNet, 1,905 lines, `x64`, with a `StartupProbe` sub-project) exists for "performance checks… not shipped with the app" — [ARCHITECTURE.md](https://github.com/isbeorn/nina/blob/develop/ARCHITECTURE.md)
- Thread-safety debt in native SDK layers was being fixed in 3.3: "Fixed **missing synchronization** in native QHY, ToupTek-family, FTDI, SBIG and Moravian SDK calls to prevent overlapping operations and **premature handle release**" — [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)
- Other 3.3 robustness fixes indicating prior defects: "The Connect All and Disconnect All commands can no longer run at the same time"; "Sun and Moon altitude calculations no longer return transient incorrect values when plugins calculate solar-system body positions concurrently"; "ASCOM cameras that report extremely large binning ranges no longer take minutes to connect"; "Image save failures in the asynchronous save queue now raise a persistent notification" — [RELEASE_NOTES.md](https://github.com/isbeorn/nina/blob/develop/RELEASE_NOTES.md)

### Inferences
- **Multi-camera**: the DI graph registers a single `CameraVM`/`ICameraMediator` as a singleton (99 `AddSingleton` registrations in `IoCBindings.cs`) and all device state is profile-scoped, so one N.I.N.A. instance drives one camera; multi-rig users run multiple instances with separate profiles. **This is inference from the DI/profile design, not a cited statement** — clone at HEAD.
- **GC pauses**: no source quantifies GC pause time, but a 13.8 GB private working set on .NET with large pinned `ushort[]` image buffers makes LOH fragmentation and Gen2 pauses the plausible mechanism behind #417's UI stutter. **Inference only — no profiling data exists in any source I could reach.**
- The maintainer's response pattern in #416 (close as "not a bug", recommend a different compression setting) and #417 (one PR, closed within two days without reporter confirmation) suggests performance issues are addressed tactically rather than through architectural rework — *inference from the two issue threads*.

### Gaps
- No published memory-footprint baseline for an idle or typical session.
- No GC/allocation profiling data, no startup-time numbers, and no `NINA.Benchmark` results are published anywhere I could reach.
- Multi-camera support could not be confirmed or denied from a citable statement.

---

## Open issue backlog survey (github.com/isbeorn/nina/issues)

### Takeaway
The GitHub tracker is **small and aggressively curated**: **13 open issues** out of **132 total** (≈119 closed) as of 2026-10-18, with the repo showing 18 open "issues" in metadata (13 issues + ~5 PRs). This is *not* the project's whole history — issues were tracked on Bitbucket before the mid-2025 GitHub migration and those (numbering into the 400s+ on Bitbucket, e.g. #381) were not migrated as GitHub issues. Themes: sequencer/target-selection logic, plate-solving/centering, plugin API gaps, and a hard `wontfix` on cross-platform.

### Cited Findings
- Totals: `is:issue` shows **132 results**; the Issues tab badge shows **13 open**, implying ~**119 closed**. Repo metadata `open_issues_count: 18` includes pull requests — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue), GitHub repo metadata
- **Complete list of the 13 open issues, sorted by 👍 reactions** (reaction counts as displayed):
  - **#411 "SKIP_IF_ALTITUDE - target condition" — 19** (opened 2026-10-01) *(the single most-requested open enhancement)*
  - **#144 "I would like to have Linux/MacOS support" — 9**, label `wontfix`
  - **#26 "Plugin developers wishlist" — 9**, label `enhancement`, pinned
  - **#425 "Feature request: Center After Drift could also check and correct field rotation (rotator connected) — field rotation from imperfect polar alignment" — 3** (2026-10-08)
  - **#330 "Um reporting incorrectly" — 3**
  - **#431 "The cooling system in the Touptek ATR294C camera is not working." — 2** (2026-10-13)
  - **#326 "Design Discussion: New Plate Solvers" — 1**, label `enhancement`
  - **#346 "[Feature Proposal] In Sequence - custom file naming" — 1**
  - **#447 "Feature request: Add 'Dome follows by N.I.N.A.' control to Advanced Sequencer" — 1** (2026-10-18)
  - **#383 "[Feature Proposal] Add ability to remove datapoint from HFR/Star Count graph"**
  - **#300 "Negative Camera GAIN don't work"**
  - **#401 "Flat Wizard can accept flats with a saturated colour channel when the overall mean meets the target"** (2026-09-29)
  - **#419 "allow to use subframes for the slew and center funtion"** (2026-10-07)
  — [isbeorn/nina issues sorted by reactions](https://github.com/isbeorn/nina/issues?q=is%3Aissue+is%3Aopen+sort%3Areactions-%2B1-desc)
- Recently closed, all within days of filing (shows triage speed): #418 (logging volume, 2026-10-06), #417 (UI sluggishness, closed 2026-10-08), #416 (XISF save time, closed 2026-10-08), #415 (nested loop runaway, closed 2026-10-08) — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue)
- **Planetary/high-speed-capture-related open issues: exactly one, #419** (subframes for slew-and-center) — and it is about plate solving, not capture. There is **no open issue requesting video/SER output or high-frame-rate capture** on the GitHub tracker — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue)
- **Performance-related open issues: none.** All four October 2026 performance reports (#415–#418) were closed within 0–2 days — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue)
- **UI-framework-related**: the only standing architectural complaint touching the UI framework is #417's request to virtualize the sequencer tree (now closed), and #400 "collapsible sequence windows" which #417 referenced. No open issue proposes replacing WPF — [issue #417](https://github.com/isbeorn/nina/issues/417)
- **Long-standing architectural complaints**:
  1. **Cross-platform** — #144, open and labelled `wontfix` with 9 reactions, is the clearest permanent architectural boundary.
  2. **Plugin API surface** — #26 is pinned, open since 2025-07-15, and still accumulating requests as late as 2026-03-28; its contents (no sequencer execution events, no stable entity IDs, reflection-based workarounds, difficulty targeting multiple N.I.N.A. versions) constitute the longest-running architectural grievance in the tracker — [issue #26](https://github.com/isbeorn/nina/issues/26)
  3. **Plate-solver abstraction** — #326 "Design Discussion: New Plate Solvers" remains an open design discussion.
  — [isbeorn/nina issues](https://github.com/isbeorn/nina/issues?q=is%3Aissue+is%3Aopen+sort%3Areactions-%2B1-desc)
- Repository has **118 forks** and **has_discussions: false**; community discussion is on Discord — GitHub repo metadata, [README.md](https://github.com/isbeorn/nina/blob/develop/README.md)

### Inferences
- A 13-issue open backlog against 8,152 commits and ~100k users-scale software means the tracker is **not** the primary channel for feature requests — Discord is. Any comparison that reads "only 13 open issues" as a quality signal would be misreading the process — *inference supported by the README pointing support at Discord and by `has_discussions: false`*.
- Issue numbering reaching #447 against 132 total issues implies roughly 315 of the numbers in the sequence are pull requests, i.e. PR throughput is ~2.4× issue throughput — *inferred arithmetic*.
- The near-total absence of planetary/high-frame-rate requests on the tracker, combined with the maintainer's ROI comments, indicates the user base self-selects away from that workload rather than that the need is met — *inference*.

### Gaps
- The pre-migration Bitbucket issue history (hundreds of issues, including the planetary/ROI discussion #381) is **no longer reachable** — direct Bitbucket issue URLs return HTTP 404. Any claim that "N.I.N.A. users have never asked for X" cannot be supported, because that archive is gone.
- I surveyed the open backlog exhaustively (all 13) but sampled only ~10 closed issues; a systematic closed-issue theme analysis was not performed.
- Reaction counts for the last four open issues were not displayed on the page and are unknown.

---

## Cross-cutting summary for the comparison author

### Cited Findings
- N.I.N.A. is free, MPL-2.0, Windows-only, .NET 10/WPF, single-dominant-maintainer, ~437k lines of C#, 19 projects, 8,152 commits since 2016-06-08, stable 3.2 (2025-11-12), nightly 3.3.0.1068 (2026-10-17), 109 plugins, 13 open GitHub issues — all sourced above.
- Direct overlap with a Win32/C++ planetary capture app is confined to: native camera SDK coverage (ZWO/QHY/ToupTek-family/Player One/SVBony/Atik/FLI/SBIG/Moravian/Canon/Nikon), ROI/subframing, contrast-detection autofocus (works on solar-system targets), plate solving and mount control. N.I.N.A. has **no video output, no frame-rate management, and no high-speed capture path** — sourced above.

### Inferences
- The two products are complements, not substitutes, and the maintainer of N.I.N.A. has said as much about ROI/planetary work. A fair comparison should be framed on architecture and extensibility (managed vs native, plugin model vs none, sequencer vs capture loop), not on feature-count parity — *inference*.
