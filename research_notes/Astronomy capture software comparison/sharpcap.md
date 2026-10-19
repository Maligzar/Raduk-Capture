# SharpCap — Technical Profile

**Research date: 18 October 2026.** All "current" statements are as of that date unless otherwise flagged.

> **Important methodological caveat for the report writer:** during this research the egress proxy **blocked all direct HTTP fetches to `www.sharpcap.co.uk`, `docs.sharpcap.co.uk`, `forums.sharpcap.co.uk`, `downloads.sharpcap.co.uk` and `web.archive.org`** (gateway returned HTTP 403 to CONNECT; DNS resolution failed for the fetch tool). Tavily CLI could not be installed for the same reason. **Every finding below therefore comes from a web-search layer that read those pages server-side and returned summaries/quotations, not from a page I rendered myself.** Direct quotations are reproduced as the search layer returned them. This reduces fidelity: exact build numbers, exact prices and exact wording should be re-verified against the live pages before publication. I flag below where this matters most.
>
> SharpCap is **closed-source commercial software**. No source code, architecture document or API reference for internals is published. Everything about internal architecture below is either (a) a developer statement on the official forum, (b) an artefact visible from outside (installer behaviour, crash-log stack frames, DLL names), or (c) explicitly labelled inference.

---

## Q1. Implementation language, runtime and UI framework

### Takeaway
SharpCap is a **managed .NET application written in C#** — this is confirmed by the developer's own user manual, which states the product name derives from the language. Current builds target **Microsoft .NET 8** and ship as a **self-contained installer** (no separate .NET install needed). The UI framework is **mixed WinForms + WPF**, evidenced by crash-log stack frames containing both `System.Windows.Forms` native-image DLLs and `System.Windows.Threading` / `SharpCapViewModel` frames; I could not find a developer statement that settles the split. Confidence: language **high**; runtime version **high**; UI framework **medium** (evidence is indirect).

### Cited Findings
- SharpCap is written in C#; the name derives from the language: *"SharpCap was easy to arrive at – it comes from the programming language it is written in – C# (pronounced C-sharp)"* — SharpCap User Manual, quoted via search of [downloads.sharpcap.co.uk/docs/SharpCapUserManual_v40.pdf](http://downloads.sharpcap.co.uk/docs/SharpCapUserManual_v40.pdf)
- The official downloads page states SharpCap 4.1 *"now runs on Microsoft .NET 8 (previously .NET 7)"* — [www.sharpcap.co.uk/sharpcap/downloads](https://www.sharpcap.co.uk/sharpcap/downloads)
- SharpCap 4.1 is a self-contained deployment: *"SharpCap 4.1 is a self-contained installer and does not need a separate '.NET Framework' installation"* — [docs.sharpcap.co.uk/4.1/3_InstallationandGettingStarted.htm](https://docs.sharpcap.co.uk/4.1/3_InstallationandGettingStarted.htm)
- Earlier SharpCap releases downloaded the .NET Framework from the internet during install and could fail without connectivity — [docs.sharpcap.co.uk/4.1/3_InstallationandGettingStarted.htm](https://docs.sharpcap.co.uk/4.1/3_InstallationandGettingStarted.htm)
- WinForms evidence: a SharpCap 3.2 crash log posted on the official forum shows *"Windows.Forms.ni.dll loaded from C:\Windows\assembly\NativeImages_v4.0.30319_32\System.Windows.Forms"* — [forums.sharpcap.co.uk/viewtopic.php?t=5](https://forums.sharpcap.co.uk/viewtopic.php?t=5)
- WPF evidence: another posted stack trace shows *"a SharpCapViewModel event handler wrapped in System.Windows.Threading stack"* (a WPF Dispatcher exception path) — [forums.sharpcap.co.uk/viewtopic.php?t=5](https://forums.sharpcap.co.uk/viewtopic.php?t=5)
- Internal type names visible in crash reports include `SharpCap.Models.Application.OnStopCapture()` and `SharpCap.Launcher` (the latter seen in a .NET `FailFast` Application-event-log entry) — [forums.sharpcap.co.uk/viewtopic.php?t=4435](https://forums.sharpcap.co.uk/viewtopic.php?t=4435), [forums.sharpcap.co.uk/viewtopic.php?t=5](https://forums.sharpcap.co.uk/viewtopic.php?t=5)
- Scripting bridge is **IronPython**, "a .NET port of Python" — [docs.sharpcap.co.uk/4.0/33_Scripting.htm](https://docs.sharpcap.co.uk/4.0/33_Scripting.htm)
- IronPython version was moved to **IronPython 3.4 / Python 3.x semantics in SharpCap 4.1** — [forums.sharpcap.co.uk/viewtopic.php?p=33960](https://forums.sharpcap.co.uk/viewtopic.php?p=33960); also listed on [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1)
- Project started February 2010, originally out of frustration with webcam-capture tools of the era — SharpCap User Manual, via [downloads.sharpcap.co.uk/docs/SharpCapUserManual_v40.pdf](http://downloads.sharpcap.co.uk/docs/SharpCapUserManual_v40.pdf)
- Author: **Robin Glover** (manual co-credited with David Richards in some editions) — [downloads.sharpcap.co.uk/docs/SharpCapUserManual_v32.pdf](http://downloads.sharpcap.co.uk/docs/SharpCapUserManual_v32.pdf)

### Inferences
- **Inferred:** The presence of both WinForms native images and a WPF `Dispatcher`/`ViewModel` stack in the same product strongly suggests an originally-WinForms codebase (2010 origin predates widespread WPF adoption for this niche) with newer dialogs/panels written in WPF, likely hosted via `ElementHost`/`WindowsFormsHost` interop. The naming `SharpCapViewModel` implies MVVM for at least part of the UI. This is **inference from stack frames, not a developer statement.**
- **Inferred:** "Self-contained installer" + ".NET 8" means the app ships its own CoreCLR and framework assemblies, so it is .NET (Core) 8, **not** .NET Framework 4.x, for 4.1 and later. Note the 3.2-era crash log shows the old .NET Framework `NativeImages_v4.0.30319` path — i.e. the migration from .NET Framework to modern .NET happened between 3.2 and 4.1.
- **Inferred (relevant to the competing Win32/C++ app):** A managed runtime implies GC pauses and a managed/native marshalling boundary at every camera-SDK call, since all vendor camera SDKs (ZWO ASI, QHY, etc.) are native C DLLs reached via P/Invoke. I found **no** published statement from the developer about pinning, unsafe buffers or GC-mode tuning; do not assert any specific mitigation.
- The recalled "WinForms" understanding is **partially** right: WinForms is clearly present, but it is not the whole story.

### Gaps
- No developer statement found that explicitly names the UI toolkit(s) or the WinForms/WPF split.
- No DLL manifest of an installed SharpCap tree obtained (the installer and docs pages were unreachable), so I cannot list the shipped assemblies, the vendor SDK DLL names, or whether any component is native C++.
- Whether SharpCap uses `Server GC`, unsafe/pinned buffers, SIMD (`System.Numerics.Vector`), or native code for the hot capture path — unpublished, and I found no forum statement.
- Exact .NET target of the 4.2 beta (assumed still .NET 8, unverified).

---

## Q2. Current released version, release date, cadence, version history

### Takeaway
As of 18 Oct 2026 the **current stable release is SharpCap 4.1** (released **22 September 2023**); **SharpCap 4.2 is still in public beta**, with build **4.2.15166.0 (64-bit) dated 21 September 2026**. Cadence for major versions is roughly every 2–2.5 years with long public beta periods and very frequent beta builds.

### Cited Findings
- SharpCap 4.1 general availability announced **22 September 2023** — [Cloudy Nights: "SharpCap 4.1 Now available"](https://www.cloudynights.com/forums/topic/892670-sharpcap-41-now-available/); also [Solar Astronomy Today](https://solarastronomytoday.com/sharpcap-4-1-has-been-released-out-of-beta/)
- SharpCap 4.0 released **June 2021** — [Cloudy Nights: "SharpCap 4.1 Now available"](https://www.cloudynights.com/forums/topic/892670-sharpcap-41-now-available/)
- The 4.2 Beta page's top entry is *"Download Version 4.2.15166.0 (64-bit) 21 September 2026"*, with earlier betas in the same list going back to 10 August 2026 — [www.sharpcap.co.uk/sharpcap/sharpcap-downloads/sharpcap-4-2-beta](https://www.sharpcap.co.uk/sharpcap/sharpcap-downloads/sharpcap-4-2-beta)
- The main downloads page still recommends 4.1 / 4.0 / 3.2 / 3.1 and says *"if you are using Windows 10 or 11, you can also try the new SharpCap 4.2 Beta version"* — [www.sharpcap.co.uk/sharpcap/downloads](https://www.sharpcap.co.uk/sharpcap/downloads)
- Beta builds are time-limited: *"beta versions have a built-in time limit"* — [About SharpCap Beta Versions](https://www.sharpcap.co.uk/?p=248)
- A January 2026 forum post states 4.2 will **drop 32-bit builds** and **drop Windows 7 / 8 / 8.1** support; the site states *"updates to SharpCap will only be available in 64-bit versions from the beginning of 2026"* — [forums.sharpcap.co.uk/viewtopic.php?t=9286](https://forums.sharpcap.co.uk/viewtopic.php?t=9286)
- Installing 4.2 beta does **not** uninstall 4.1 — side-by-side install supported — [SharpCap 4.2 Beta page](https://www.sharpcap.co.uk/sharpcap/sharpcap-downloads/sharpcap-4-2-beta)
- 4.2 beta features confirmed so far: **RAW camera-file reading via the libraw library** in the Folder Monitor Camera and test cameras (DNG, CR2 etc.); a **satellite forecasting tool** (May 2026); a **light-pollution / sky-brightness tool** added in the 24 August 2026 build — [www.sharpcap.co.uk/?p=2423](https://www.sharpcap.co.uk/?p=2423), [forums.sharpcap.co.uk/viewtopic.php?t=9513](https://forums.sharpcap.co.uk/viewtopic.php?t=9513), [SharpCap 4.2 Beta page](https://www.sharpcap.co.uk/sharpcap/sharpcap-downloads/sharpcap-4-2-beta)
- A forum reply notes the final version number is not fixed: *"a last minute change to 5.0 from 4.2 will not cause any problems"* — [forums.sharpcap.co.uk/viewtopic.php?t=9574](https://forums.sharpcap.co.uk/viewtopic.php?t=9574)
- A 4.2 feature poll (Autofocus Parameter Editor, Improved Spiral Search, First Use Setup Wizard, Remote Notifications) was won by the **Autofocus Parameter Editor** — [forums.sharpcap.co.uk/viewtopic.php?p=54118](https://forums.sharpcap.co.uk/viewtopic.php?p=54118)
- Windows support matrix on the downloads page: Windows 11, 10, 8.1, 7 SP1 → SharpCap 4.1/4.0/3.2/3.1; Windows 8, 7 (no SP), Vista SP2 → SharpCap 3.0; **Windows XP no longer supported** — [www.sharpcap.co.uk/sharpcap/downloads](https://www.sharpcap.co.uk/sharpcap/downloads)
- Third-party listing (Softpedia, updated June 2026) gives latest stable as **4.1.14304.0** and beta **4.2.14701.0** — [softpedia.com/get/Others/Home-Education/SharpCap.shtml](https://www.softpedia.com/get/Others/Home-Education/SharpCap.shtml) — treat as secondary/stale relative to the official pages.

### Inferences
- **Inferred:** 4.1 stable was still receiving maintenance builds into 2026 (Softpedia's 4.1.14304.0 is a far higher build number than the 2023 GA), so "4.1 released 2023" understates how current the stable line is. The build-number scheme appears to be days-since-epoch-like and monotonic across 4.1/4.2 (14304 vs 14701 vs 15166), i.e. a single trunk.
- **Inferred:** Major-version cadence ≈ 2–2.5 years (4.0 Jun 2021 → 4.1 Sep 2023 → 4.2 still in beta Oct 2026 = 3+ years), with 4.2 running notably longer than the previous gap.

### Gaps
- **I could not confirm whether SharpCap 4.2 went to stable release between 21 Sep 2026 and 18 Oct 2026.** The most recent evidence (Sep 2026) shows it in beta. Re-check the downloads page before publication.
- Exact current 4.1 stable build number as of Oct 2026 — not verified from an official page.
- No consolidated official changelog/version-history page was reachable; 4.2's full feature list is unavailable.

---

## Q3. Licensing and pricing (free vs SharpCap Pro)

### Takeaway
SharpCap is free for basic camera control and capture; advanced features require **SharpCap Pro**, an annual subscription at **£14/year for a personal licence** (raised from £12 in April 2024), with other currencies offered and a lifetime licence available only as an upgrade from an existing annual licence. **Over 30 features are Pro-gated as of 4.1.** Pro features can be *used* unlicensed in an evaluation mode, but **saving is disabled** until restart.

### Cited Findings
- *"SharpCap licenses cost £14 per year for a personal license"*; other currencies (US$, Euro, AUS$, JPY) available — [SharpCap Pro FAQ](https://www.sharpcap.co.uk/?p=411)
- April 2024 price rise from £12 to £14 to offset inflation; US$ pricing described as unchanged at the £18/year-equivalent level — [forums.sharpcap.co.uk/viewtopic.php?p=42457](https://forums.sharpcap.co.uk/viewtopic.php?p=42457) and [SharpCap Pro Pricing Update](https://www.sharpcap.co.uk/?p=2179)
- Lifetime licence exists but *"you cannot buy a lifetime license straight away if you do not yet have a SharpCap Pro license"* — you buy a one-year licence first then upgrade, with a discount for what you already paid — [Can I buy a permanent SharpCap Pro license?](https://www.sharpcap.co.uk/?p=1698)
- Free tier: *"Simply working with a camera and capturing images or videos does not require a SharpCap Pro license"*; basic live stacking is free — [SharpCap Free | SharpCap PRO](https://www.sharpcap.co.uk/?p=915)
- Pro-gated features include: the **final stage of polar alignment**, **live flat-frame correction**, **dark subtraction**, **satellite-trail removal**, **background subtraction**, **advanced live stacking**, **planetary/solar/lunar live stacking**, **automated imaging sequencers**, **automated focus scans**, and **Python scripting** — [SharpCap Free | SharpCap PRO](https://www.sharpcap.co.uk/?p=915), [docs.sharpcap.co.uk/4.0/4_SharpCapPro.htm](https://docs.sharpcap.co.uk/4.0/4_SharpCapPro.htm), [docs.sharpcap.co.uk/4.1/15_AdvancedTopics.htm](https://docs.sharpcap.co.uk/4.1/15_AdvancedTopics.htm)
- *"With the introduction of SharpCap 4.1, there are now over 30 key SharpCap Pro features"* — [SharpCap news](https://www.sharpcap.co.uk/category/news)
- Evaluation mode: most Pro features can be tried unlicensed, but SharpCap enters a mode where *"you cannot save any images until you close and then re-start the application"* — [docs.sharpcap.co.uk/4.0/4_SharpCapPro.htm](https://docs.sharpcap.co.uk/4.0/4_SharpCapPro.htm)
- On licence lapse, *"SharpCap will go back to being the free version automatically"* — [SharpCap Pro FAQ](https://www.sharpcap.co.uk/?p=411)
- Developer is explicit that Pro is not support: a Pro licence *"does not fix bugs or improve basic features"* — [SharpCap isn't working right for me – will buying a Pro license fix the problem?](https://www.sharpcap.co.uk/ufaqs/sharpcap-isnt-working-right-for-me-will-buying-a-pro-license-fix-the-problem)

### Inferences
- **Inferred:** The paywall line is drawn at *automation and image-quality processing*, not at *capture*. A pure planetary-capture user (ROI video to SER) can run entirely free; the Pro value is in EAA/deep-sky live stacking, sequencing, autofocus and polar alignment. This is a materially different monetisation posture from a one-off-purchase or wholly-free competitor.
- **Inferred:** The £14 figure is the **2024** price. I found no 2025 or 2026 price announcement, so £14/year is the last confirmed price, not a verified-2026 price.

### Gaps
- **Current lifetime-licence price (any currency) — not found.** The 2024 post says lifetime prices would be "updated to match the new pricing" but no figure is published in anything I could reach.
- Exact USD/EUR figures as of 2026 — not confirmed. One forum post cites an older £10/$15/€12 tier; another cites "£18/year" for US$ payers. These conflict and are from different dates; **do not state a USD price without re-verification.**
- Whether per-seat/commercial/educational licensing differs from "personal licence" — not found.

---

## Q4. Hardware abstraction — camera and accessory support

### Takeaway
SharpCap supports **~14 named native astro-camera vendor SDKs**, plus DirectShow/Windows webcams, plus ASCOM cameras and ASCOM Alpaca remote cameras. ASCOM mounts, focusers, filter wheels and switches are supported; rotator support is contradicted between the website feature table and a developer forum reply.

### Cited Findings
- Native (vendor-SDK) camera families named in the manual: **Altair, AstCamPan, AstroAsis, Atik, Basler, Celestron, Imaging Source, Moravian Instruments, Player One, Point Grey, QHY, SVBony, Starlight Xpress, ZWO** (14 families) — SharpCap manual via [docs.sharpcap.co.uk/4.0/3_Requirements.htm](https://docs.sharpcap.co.uk/4.0/3_Requirements.htm) and [sharpcap.co.uk](https://sharpcap.co.uk/)
- Vendor caveats published on the homepage: *"Celestron/Imaging Source cameras (selected models, only supports SDK 3.4 not 4.0)"*; *"Point Grey (now FLIR) cameras (Flycapture SDK cameras, not Spinnaker cameras)"*; Basler support is for cameras using the **Basler Pylon** drivers — [sharpcap.co.uk](https://sharpcap.co.uk/)
- Version-gated vendor support: Atik and Moravian from **3.2**; Player One from **4.0** — [sharpcap.co.uk](https://sharpcap.co.uk/)
- Fallback paths: *"For cameras without direct support, SharpCap can use ASCOM drivers, and many webcams work through a Windows Webcam driver"* — [docs.sharpcap.co.uk/4.0/3_Requirements.htm](https://docs.sharpcap.co.uk/4.0/3_Requirements.htm)
- Native support is characterised as *"the preferred approach as it provides access to full functionality of the camera manufacturer's SDK but requires official support by Robin"* — [The Unofficial Guide to Using SharpCap](https://www.scribd.com/document/667462781/The-Unofficial-Guide-to-Using-SharpCap-v0-1) (2017-era third-party guide)
- **ASCOM Alpaca cameras** supported: cameras *"that can be controlled using the 'ASCOM Alpaca' remote control system"* are included — [sharpcap.co.uk](https://sharpcap.co.uk/); remote hardware support via **ASCOM Remote / Alpaca** was a 4.1 feature — [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1)
- ASCOM mounts: *"SharpCap can connect to and control most ASCOM mounts"*, subject to driver capability; mounts lacking `MoveAxis` cannot be slewed from SharpCap — [docs.sharpcap.co.uk/4.1/12_TelescopeHardwareControl.htm](https://docs.sharpcap.co.uk/4.1/12_TelescopeHardwareControl.htm)
- ASCOM focuser (In/Out control) and ASCOM filter wheel (drop-down with driver-configured filter names) supported — [docs.sharpcap.co.uk/4.0/28_TelescopeHardwareControl.htm](https://docs.sharpcap.co.uk/4.0/28_TelescopeHardwareControl.htm)
- **ASCOM Switch** support added in 4.1 — [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1)
- **Rotators — conflicting sources:** the website feature table lists rotators among supported ASCOM devices, but a developer forum reply states *"there's no support for field rotators in SharpCap"* — [sharpcap.co.uk](https://sharpcap.co.uk/) vs [forums.sharpcap.co.uk/viewtopic.php?p=19021](https://forums.sharpcap.co.uk/viewtopic.php?p=19021). The forum post is older; unresolved.
- The site's feature table also mentions **Indigo** alongside ASCOM Remote for remote hardware — [sharpcap.co.uk](https://sharpcap.co.uk/)
- 4.1 added **hardware reconnection** — SharpCap attempts to reconnect cameras and other devices after brief disconnects — [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1)
- **PHD2** integration exists but is only connected during live stacking or sequencer runs, not during a manually started focus run — [forums.sharpcap.co.uk/viewtopic.php?p=49070](https://forums.sharpcap.co.uk/viewtopic.php?p=49070)
- 4.2 beta adds a **Folder Monitor Camera** that reads DSLR RAW files (DNG, CR2…) via **libraw** — [www.sharpcap.co.uk/?p=2423](https://www.sharpcap.co.uk/?p=2423)

### Inferences
- **Inferred:** 14 named native families is the count from the manual's list; the homepage list and the manual list differ in detail (search results explicitly note *"the manual and the homepage also differ"*), so **"approximately 14"** is safer than a hard number. Add ToupTek/ToupCam (an active forum support topic exists) — but I could not confirm it is in the official native list, so do not count it.
- **Inferred:** The "SDK 3.4 not 4.0" note for Imaging Source/Celestron implies SharpCap links a pinned, older vendor SDK version — a maintenance liability and a concrete point of comparison for a competitor that tracks current SDKs.
- **Inferred:** Everything non-native routes through ASCOM, which on Windows is a COM (in-proc/out-of-proc) interface — i.e. the abstraction layers are: native vendor SDK (P/Invoke) → ASCOM COM → DirectShow. No evidence of an INDI client.

### Gaps
- No complete, authoritative per-model camera compatibility list obtained (the official pages were unreachable).
- Whether Alpaca works for **mounts/focusers/filter wheels** as well as cameras — not confirmed.
- Rotator support — unresolved contradiction (see above).
- No information on how many distinct *models* are supported, only families.

---

## Q5. High-frame-rate planetary capture pipeline

### Takeaway
SharpCap's capture pipeline is **camera → RAM frame buffer → disk writer**, with the RAM buffer absorbing short-term disk-throughput deficits and **frames being discarded once the buffer is full**. Output formats are SER and AVI for video, PNG/TIFF/FITS for stills, with automatic format selection at a 5-second exposure threshold. Capture can be limited by frame count or elapsed time. A built-in disk **Speed Test** writes a test SER file to measure sustained throughput. The developer has publicly stated the **SER per-frame timestamp is the frame-arrival time from the camera SDK, not a mid-exposure estimate** — a precision caveat for WinJUPOS derotation.

### Cited Findings
- Buffering and drop behaviour (developer): if drops occur only while recording, *"the disk is likely too slow for the camera's data rate. Memory buffering covers the gap for a while, then frames start to go missing."* Remedies offered: lower frame rate, smaller ROI, faster drive — [forums.sharpcap.co.uk/viewtopic.php?p=9414](https://forums.sharpcap.co.uk/viewtopic.php?p=9414)
- Once the buffer fills, *"the capture rate falls to whatever the SSD can absorb, and the camera's frames are discarded until a memory slot frees up"* — [forums.sharpcap.co.uk/viewtopic.php?p=36536](https://forums.sharpcap.co.uk/viewtopic.php?p=36536)
- Buffer occupancy is exposed: the **Memory readout in the status bar** shows the buffer filling — [forums.sharpcap.co.uk/viewtopic.php?p=37019](https://forums.sharpcap.co.uk/viewtopic.php?p=37019)
- **Speed Test button** on the filename settings page writes test data to an SER file to measure the drive's real sustained write rate; real rates can fall well below rated speed on a near-full or un-TRIMmed SSD — [forums.sharpcap.co.uk/viewtopic.php?p=36536](https://forums.sharpcap.co.uk/viewtopic.php?p=36536)
- Developer diagnosis of an ASI662MC-on-NVMe drop case: either frames lost before reaching SharpCap, or **CPU saturation during saving**; a capture drive sharing a USB hub with the camera was flagged as a likely cause; the **SharpCap log records the cause of drops during capture** — [forums.sharpcap.co.uk/viewtopic.php?p=36527](https://forums.sharpcap.co.uk/viewtopic.php?p=36527)
- Output-format cost: saving individual PNG files *"is much slower than SER"* and can produce drops that look like a disk problem — [forums.sharpcap.co.uk/viewtopic.php?p=32647](https://forums.sharpcap.co.uk/viewtopic.php?p=32647)
- Formats: Capture Format chooser separates **video (SER, AVI)** from **stills (PNG, TIFF, FITS and others)** — [docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm](https://docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm)
- Auto format selection: *"for short exposures (<5s) a video format will be chosen, for longer exposures a still format will be chosen"*; a Snapshot button *"always takes a still image"* — [docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm](https://docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm)
- Capture limits: *"The Start Capture option allows a custom capture limited either by number of frames or time to be started."* With a still format selected, multiple files are written into one folder instead of a single video file — [docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm](https://docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm)
- Official format guidance: SER for sun/moon/planets video; RAW12/14/16 or MONO12/14/16 to **FITS** for deep-sky stills; AVI + PNG for general imaging — [sharpcap.co.uk/ufaq-category/getting-started](https://www.sharpcap.co.uk/ufaq-category/getting-started)
- **Timestamping (important):** developer Robin Glover has stated the **SER timestamp reflects when a frame arrived from the camera SDK, while FITS subtracts the exposure time**, and that *"the SER code hasn't been updated to the better estimate"* — [forums.sharpcap.co.uk/viewtopic.php?p=37887](https://forums.sharpcap.co.uk/viewtopic.php?p=37887)
- SharpCap takes time from the Windows system clock and *"largely works in UTC time rather than local time"* — [forums.sharpcap.co.uk/viewtopic.php?p=8372](https://forums.sharpcap.co.uk/viewtopic.php?p=8372)
- **WinJUPOS-compatible filenames** are an explicit checkbox on the Filenames settings page; there is a separate *"Use UTC times in file and folder names"* option — [forums.sharpcap.co.uk/viewtopic.php?p=31895](https://forums.sharpcap.co.uk/viewtopic.php?p=31895)
- AVI 2 GB size limit is a **user report, not documented**: *"a size limit of 2Gb (sometimes)"*, with the same user noting SER *"seems much better defined and would certainly be a good choice for high speed planetary imaging"* — [forums.sharpcap.co.uk/viewtopic.php?p=29066](https://forums.sharpcap.co.uk/viewtopic.php?p=29066)

### Inferences
- **Inferred:** The architecture is a classic producer/consumer: an SDK callback or poll thread enqueues frames into a bounded in-RAM pool; a writer thread drains to disk; overflow drops the newest frame. The "Memory slot frees up" wording and the per-capture memory readout both point to a **fixed-size preallocated buffer pool**, which is the right design for a managed runtime (avoids per-frame GC allocation). This is inference from developer wording, **not** a published design.
- **Inferred:** The SER timestamp semantics (arrival-time, no exposure-midpoint correction) is a genuine, developer-acknowledged accuracy defect for WinJUPOS derotation and for occultation timing. This is the single most concrete technical criticism found, and it is **self-reported by the author** — strong evidence. Date of the statement is not established beyond "posted on the forum"; it may have been fixed since. Flag as unverified-current.
- **Inferred:** The documented remediation advice (smaller ROI, lower frame rate, faster drive, keep the capture drive off the camera's USB hub) implies no on-the-fly compression in the capture path for SER.

### Gaps
- **No published or measured frame-rate figures for SharpCap specifically.** I found no benchmark, official or community, giving fps at a given ROI/bit depth. Any fps numbers in the final report must come from elsewhere or be omitted.
- No documentation of ROI granularity, binning behaviour at high frame rate, or whether the ROI is applied in-camera vs in software (almost certainly in-camera via the SDK, but unconfirmed).
- No statement about whether SharpCap uses overlapped/unbuffered I/O (`FILE_FLAG_NO_BUFFERING`), memory-mapped writes, or ordinary buffered `FileStream` writes.
- No confirmation of the AVI 2 GB limit from official docs.
- Whether SER timestamp semantics were corrected in 4.1/4.2 — unknown.

---

## Q6. Live stacking

### Takeaway
Live stacking aligns on the **brightest stars** between frames to correct gradual drift and field rotation, targeting short (≤~30 s) exposures on unguided/alt-az mounts. Its stated purpose is **noise reduction, not brightening**. Practical limit around **~32,000 frames** in 16-bit accumulation before highlights clip. Star-based alignment fails where there are no stars, and the developer has stated **planetary live stacking at 30 fps+ is hard** because of the per-frame processing budget — though 4.1 did ship a separate lunar/solar/planetary live stack.

### Cited Findings
- Design intent: *"capturing deep sky images doesn't require a high-accuracy, guided, equatorial mount and long sub-frame exposures"* — [docs.sharpcap.co.uk/4.0/16_LiveStacking.htm](https://docs.sharpcap.co.uk/4.0/16_LiveStacking.htm)
- Alignment method: follows the brightest stars between frames, *"correcting gradual drift or rotation between successive frames"*; suits low-noise CMOS and exposures of ~30 s or less — [docs.sharpcap.co.uk/4.0/16_LiveStacking.htm](https://docs.sharpcap.co.uk/4.0/16_LiveStacking.htm)
- Effect: *"reveal more detail with less noise as the number of frames builds up"* while overall brightness stays roughly the same; users raise the stretch as the stack grows — [docs.sharpcap.co.uk/4.0/16_LiveStacking.htm](https://docs.sharpcap.co.uk/4.0/16_LiveStacking.htm)
- Stack-length limit (developer estimate, 16-bit data): *"about 32000 frames can be added before the bright parts get too bright"* — [forums.sharpcap.co.uk/viewtopic.php?p=34918](https://forums.sharpcap.co.uk/viewtopic.php?p=34918)
- Planetary live stacking scepticism (developer): doubted good live planetary stacking is feasible *"since planetary imaging is usually run at 30fps or faster, leaving very little processing time per frame"* — [forums.sharpcap.co.uk/viewtopic.php?p=1183](https://forums.sharpcap.co.uk/viewtopic.php?p=1183)
- Star-based alignment *"does not help when there are no stars to track"* — [forums.sharpcap.co.uk/viewtopic.php?p=1183](https://forums.sharpcap.co.uk/viewtopic.php?p=1183)
- Some cameras keep a high frame rate during long exposures by resending the same frame, which the developer said *"is not ideal for live stacking"*; and if frame rate and exposure do not match, *"some of your frames will not be stacked"* — [forums.sharpcap.co.uk/viewtopic.php?p=4649](https://forums.sharpcap.co.uk/viewtopic.php?p=4649)
- **4.1 added lunar/solar/planetary live stacking** — *"lets you filter, stack, and sharpen frames while observing"* — [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1)
- Pro-gated live-stack processing: live flat-frame correction, dark subtraction, satellite-trail removal, background subtraction, "advanced" live stacking — [SharpCap Free | SharpCap PRO](https://www.sharpcap.co.uk/?p=915)
- A 4 Jan 2023 4.1 beta added **non-linear background gradient removal** — [forums.sharpcap.co.uk/viewtopic.php?p=33791](https://forums.sharpcap.co.uk/viewtopic.php?p=33791)
- Memory sizing guidance (developer), two simultaneous live stacks with ASI2400 full-frame cameras: *"16Gb would be fine… 12Gb probably OK, 8Gb dubious and less than 8Gb very little chance"*; sharpening and noise reduction increase memory use — [forums.sharpcap.co.uk/viewtopic.php?p=52412](https://forums.sharpcap.co.uk/viewtopic.php?p=52412)

### Inferences
- **Inferred:** "32,000 frames before the bright parts get too bright" implies the live-stack accumulator is a **16-bit integer sum** rather than 32-bit float — 65535/2 ≈ 32,768 matches the quoted figure for mid-level pixels. This is inference from arithmetic consistency, not a published fact.
- **Inferred:** The 4.1 planetary live stack is a *display/preview* aid rather than a replacement for AutoStakkert!/Registax post-processing — the developer's earlier scepticism about the per-frame budget was never retracted in anything I found.
- **Inferred:** Live stacking writes FITS output, but I did not find an explicit statement of the output format(s) in a reachable source — **do not assert** FITS live-stack output without verification.

### Gaps
- Live-stack **output file format and path** (FITS? PNG? what header metadata?) — not confirmed from a reachable source.
- Whether alignment is translation-only, translation+rotation, or affine — the docs say "drift or rotation", implying at least similarity transform, but the exact model and interpolation kernel are unpublished.
- Number of alignment stars used, and the detection/centroiding algorithm — unpublished.

---

## Q7. Distinguishing feature set

### Takeaway
SharpCap's differentiators are mostly **measurement and automation** features that a pure capture app lacks: a plate-solving **polar alignment** routine with an offline near-pole star database, **Sensor Analysis + Smart Histogram ("the Brain")** which computes optimal gain/exposure from measured read noise and measured sky background, a multi-modal **focus assistant** (multi-star FWHM, HFD, Bahtinov), **plate-solved Push-To**, a **sequencer**, and **mosaic planning**.

### Cited Findings

**Polar alignment**
- Two-image method: take one image, rotate the mount ~90° about RA, take a second; stars in both frames reveal the sky area covered and *"the point the sky appears to pivot around"* — [docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm](https://docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm)
- Built-in plate solver runs *"without internet or external databases"* — [docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm](https://docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm)
- Solver range is **near-pole only**: docs give **6–7 degrees in 4.1**, 5 degrees in older versions; a forum admin described the database as *"a 5-degree circle around each pole"* — [docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm](https://docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm); [forums.sharpcap.co.uk/viewtopic.php?p=554](https://forums.sharpcap.co.uk/viewtopic.php?p=554)
- Error computation: the solver knows where the telescope points in each frame, so it locates the celestial pole; it separately derives the **RA axis position from the rotation centre**; the angular separation of the two is the polar-alignment error, corrected via alt/az knobs — [docs.sharpcap.co.uk/4.0/18_PolarAlignment.htm](https://docs.sharpcap.co.uk/4.0/18_PolarAlignment.htm)
- Rotation is manual (RA clutch or GoTo) and *"does not need a precise angle, only an approximate one"* — [www.sharpcap.co.uk/?p=67](https://www.sharpcap.co.uk/?p=67)
- Known failure mode: *"plate solving often fails in sparse star fields, and the rotated position can be harder to solve than the home position"* — [www.sharpcap.co.uk/?p=144](https://www.sharpcap.co.uk/?p=144), [forums.sharpcap.co.uk/viewtopic.php?p=1072](https://forums.sharpcap.co.uk/viewtopic.php?p=1072)
- The **final adjustment stage is Pro-gated** — [SharpCap Free | SharpCap PRO](https://www.sharpcap.co.uk/?p=915)

**Sensor Analysis / Smart Histogram / Brain**
- Combined with Sensor Analysis results, *"SharpCap can measure the sky background brightness for you"* and simulates how different gain/exposure settings would affect the final stack — [docs.sharpcap.co.uk/4.0/15_SmartHistogram.htm](https://docs.sharpcap.co.uk/4.0/15_SmartHistogram.htm)
- Histogram header bar: *"The top bar with the red, amber and green sections shows the impact of the camera's read noise on the total image noise at that brightness level"* — [docs.sharpcap.co.uk/4.0/15_SmartHistogram.htm](https://docs.sharpcap.co.uk/4.0/15_SmartHistogram.htm)
- Stated goal (developer): *"point you to the exposure time that gives best results in the final stack *and no longer*"* — [forums.sharpcap.co.uk/viewtopic.php?p=2050](https://forums.sharpcap.co.uk/viewtopic.php?p=2050)
- SharpCap ships **pre-installed sensor data for popular camera models**; a self-run analysis typically lands *"within a few percent of the ones that are in the pre-installed data"* — [docs.sharpcap.co.uk/4.0/15_SmartHistogram.htm](https://docs.sharpcap.co.uk/4.0/15_SmartHistogram.htm), [forums.sharpcap.co.uk/viewtopic.php?p=2050](https://forums.sharpcap.co.uk/viewtopic.php?p=2050)
- Reported defects: on a QHY168C the Brain set gain to 15 and produced a saturated image, attributed to drivers *"never fixed"*; a 2×2 default binning setting prevented SharpCap loading saved sensor analysis at startup (fixed by defaulting to 1×1) — [forums.sharpcap.co.uk/viewtopic.php?p=18769](https://forums.sharpcap.co.uk/viewtopic.php?p=18769), [forums.sharpcap.co.uk/viewtopic.php?p=23393](https://forums.sharpcap.co.uk/viewtopic.php?p=23393)

**Focus aids**
- **Multi-star FWHM:** measures the FWHM of all suitable stars in the frame and averages them; lower = better focus — [www.sharpcap.co.uk/?p=89](https://www.sharpcap.co.uk/?p=89), [docs.sharpcap.co.uk/3.2/17_Focusing.htm](https://docs.sharpcap.co.uk/3.2/17_Focusing.htm)
- **Bahtinov mask** score approaches zero at best focus; needs a mask and a selected region around one star — [docs.sharpcap.co.uk/4.0/17_Focusing.htm](https://docs.sharpcap.co.uk/4.0/17_Focusing.htm)
- FWHM calculations were **improved in 4.0**, which added a **half-flux-diameter (HFD)** option described as less noise-sensitive — [forums.sharpcap.co.uk/viewtopic.php?p=25897](https://forums.sharpcap.co.uk/viewtopic.php?p=25897), [docs.sharpcap.co.uk/4.0/17_Focusing.htm](https://docs.sharpcap.co.uk/4.0/17_Focusing.htm)
- Practitioner tuning advice with a ZWO EAF: tune until SharpCap detects **75–100 stars**; backlash can skew results — [forums.sharpcap.co.uk/viewtopic.php?p=25078](https://forums.sharpcap.co.uk/viewtopic.php?p=25078)
- Automated focus scans are **Pro-gated** — [SharpCap Free | SharpCap PRO](https://www.sharpcap.co.uk/?p=915)
- 4.1 included **autofocus refinements**; the top-voted 4.2 feature is an **Autofocus Parameter Editor** — [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1), [forums.sharpcap.co.uk/viewtopic.php?p=54118](https://forums.sharpcap.co.uk/viewtopic.php?p=54118)

**Sequencer, plate solving, EAA**
- **Deep Sky Sequence Planner** can automatically shorten exposures during plate solving and focusing — [forums.sharpcap.co.uk/viewtopic.php?p=49070](https://forums.sharpcap.co.uk/viewtopic.php?p=49070)
- 4.1 added a **Solar system sequence planner** (automates video captures with filter changes), a **lunar and solar mosaic planner**, and a **Push-To Assistant** using plate solving to find targets with a Dobsonian or other manual scope — [What's New in SharpCap 4.1](https://www.sharpcap.co.uk/news/whats-new-in-sharpcap-4-1)
- An introductory forum thread exists for the sequencer — [forums.sharpcap.co.uk/viewtopic.php?p=27119](https://forums.sharpcap.co.uk/viewtopic.php?p=27119)

**Multi-camera / multi-instance**
- No in-app multi-camera: *"you will need to run two copies of SharpCap, one for each camera"* — [forums.sharpcap.co.uk/viewtopic.php?p=28505](https://forums.sharpcap.co.uk/viewtopic.php?p=28505)
- Users confirm it works (e.g. ASI174MM + ASI432MM concurrently), but *"if you try to connect two ZWO 432mm cameras all sorts of issues/conflicts arise"* — [forums.sharpcap.co.uk/viewtopic.php?p=17339](https://forums.sharpcap.co.uk/viewtopic.php?p=17339)
- Settings collision with identical models: settings are stored in a hidden profile named after the camera, so *"if the cameras are both the same model then this will be overwritten each time you close SharpCap"*; 4.0 added a command-line option for separate profiles, though autosave profiles are still shared — [forums.sharpcap.co.uk/viewtopic.php?p=46598](https://forums.sharpcap.co.uk/viewtopic.php?p=46598)
- Instances cannot coordinate: *"there is no way to synchronize the two copies so that copy B knows that copy A is trying to dither"* — [forums.sharpcap.co.uk/viewtopic.php?p=28505](https://forums.sharpcap.co.uk/viewtopic.php?p=28505)

### Inferences
- **Inferred:** The polar-alignment plate solver is a **purpose-built, bundled, near-pole-only star-index solver**, not a general astrometry.net/ASTAP solve — the 5–7° radius and "no external databases" together imply a small embedded catalogue around each celestial pole. The general plate solving used for Push-To is probably a different code path (possibly external solvers), but I found no source confirming which solvers SharpCap can drive.
- **Inferred:** Sensor Analysis is effectively an in-app photon-transfer-curve measurement producing read noise (e-) and gain (e-/ADU) vs gain setting; the shipped database means most users never run it. The "Brain" then solves for the exposure at which sky-background shot noise dominates read noise. This is the standard interpretation, consistent with the quoted docs, but the actual algorithm is unpublished.
- **Inferred:** Multi-instance rather than multi-camera is an architectural statement: the application model is **one camera per process**. For a comparison report this is a real limitation versus an app with a multi-device session model.

### Gaps
- Which plate-solving backends SharpCap can use for general (non-polar) solving (ASTAP? astrometry.net local? Platesolve2?) — not established from a reachable source.
- Dark/flat **library** management (as opposed to single dark/flat frames) — I found only references to live dark subtraction and live flat correction as Pro features, not to a managed library. Do not assert a dark/flat library exists.
- No evidence found about any **plugin model** beyond scripting (see Q8).

---

## Q8. Scripting and extensibility

### Takeaway
SharpCap exposes an **IronPython** console (IronPython 3.4 / Python 3.x as of 4.1) with a global `SharpCap` object rooting a .NET object graph for app, camera and control access. It is **Pro-gated**. Because IronPython cannot load CPython C extensions, **NumPy, SciPy and AstroPy are unavailable** — a significant restriction for scientific scripting. No separate compiled plugin/add-in model was found.

### Cited Findings
- Scripting language is *"IronPython, a .NET port of Python"*; the console is *"an IDE for writing, running, and debugging code"* — [docs.sharpcap.co.uk/4.0/33_Scripting.htm](https://docs.sharpcap.co.uk/4.0/33_Scripting.htm)
- The `SharpCap` object *"handles control of the application"* and is loaded automatically into each session; examples: `SharpCap.SelectedCamera = SharpCap.Cameras[0]`, `SharpCap.SelectedCamera.Controls.Exposure.Value` (milliseconds) — [docs.sharpcap.co.uk/4.0/33_Scripting.htm](https://docs.sharpcap.co.uk/4.0/33_Scripting.htm)
- `help()` in the console gives basic help output; the top pane runs single lines on Enter, the lower pad runs longer scripts via Run; a stop button aborts — [docs.sharpcap.co.uk/4.0/33_Scripting.htm](https://docs.sharpcap.co.uk/4.0/33_Scripting.htm)
- **Python Scripting is a SharpCap Pro feature** — [docs.sharpcap.co.uk/4.1/15_AdvancedTopics.htm](https://docs.sharpcap.co.uk/4.1/15_AdvancedTopics.htm)
- Updated to **IronPython 3.4 in the SharpCap 4.1 beta**; a moderator stated *"NumPy, SciPy, and AstroPy are not supported because they depend on C code"* — [forums.sharpcap.co.uk/viewtopic.php?p=33960](https://forums.sharpcap.co.uk/viewtopic.php?p=33960)
- Autocomplete only works in the upper console pane, not the script body — [forums.sharpcap.co.uk/viewtopic.php?p=41847](https://forums.sharpcap.co.uk/viewtopic.php?p=41847)
- Scripts can trigger the autofocus **sequencer action** but (as of the 3.x era) *"cannot control the manual in/out scan"* — [forums.sharpcap.co.uk/viewtopic.php?p=18516](https://forums.sharpcap.co.uk/viewtopic.php?p=18516)
- Users have hit `AttributeError: 'SharpCap' object has no attribute 'Cameras'`, attributed by moderators to old versions or to the script shadowing the `SharpCap` global — [forums.sharpcap.co.uk/viewtopic.php?p=49151](https://forums.sharpcap.co.uk/viewtopic.php?p=49151)
- Inter-instance coordination is suggested to be achievable via scripting, e.g. *"a web UI in one instance that the other messages"* — [forums.sharpcap.co.uk/viewtopic.php?p=28505](https://forums.sharpcap.co.uk/viewtopic.php?p=28505)
- Overview page: [SharpCap Scripting](https://www.sharpcap.co.uk/?p=93)

### Inferences
- **Inferred:** The `SharpCap` global is a direct projection of internal .NET objects into IronPython (property chains like `.SelectedCamera.Controls.Exposure.Value` are .NET property access, not a serialised RPC API). That means the scripting surface is essentially "whatever is public on the internal object model" — powerful, but version-fragile and undocumented as a stable contract. The `AttributeError` reports are consistent with this.
- **Inferred:** The IronPython choice follows directly from the C#/.NET implementation; it is the cheapest way to get a scripting console in-process. The cost — no C-extension ecosystem — is a direct, structural consequence of the language choice, and is a fair point of technical comparison against a native app embedding CPython.
- **Inferred:** No evidence of a documented plugin/add-in SDK; extensibility appears to be scripting-only. Absence of evidence here, given the unreachable docs, is weak — but no source mentioned a plugin model.

### Gaps
- No published API reference for the `SharpCap` object graph was found in a reachable source.
- Whether a compiled .NET plugin/add-in model exists — not established.
- Whether scripts can run headless / on startup / from the command line — not established.

---

## Q9. Performance characteristics and known limitations

### Takeaway
Windows-only, **64-bit-only from 2026**, managed .NET 8. The documented performance ceiling is **disk write throughput and CPU during save**, with an in-RAM buffer as the shock absorber. Memory guidance scales with sensor size (16 GB for two simultaneous full-frame live stacks). **No evidence of GPU acceleration was found anywhere.**

### Cited Findings
- **Windows-only.** All supported-OS statements on the downloads page are Windows versions; no macOS or Linux build is offered — [www.sharpcap.co.uk/sharpcap/downloads](https://www.sharpcap.co.uk/sharpcap/downloads)
- From the beginning of 2026, *"updates to SharpCap will only be available in 64-bit versions"*; 4.2 drops 32-bit and drops Windows 7/8/8.1 — [forums.sharpcap.co.uk/viewtopic.php?t=9286](https://forums.sharpcap.co.uk/viewtopic.php?t=9286)
- Original rationale for 64-bit (3.2 planning thread): *"more memory available easily - particularly important with people starting to use full frame 35 megapixel cameras for live stacking"* — [forums.sharpcap.co.uk/viewtopic.php?p=4051](https://forums.sharpcap.co.uk/viewtopic.php?p=4051)
- Memory sizing (developer), two simultaneous ASI2400 live stacks: *"16Gb would be fine… 12Gb probably OK, 8Gb dubious and less than 8Gb very little chance"* — [forums.sharpcap.co.uk/viewtopic.php?p=52412](https://forums.sharpcap.co.uk/viewtopic.php?p=52412)
- Bottleneck is disk, then CPU during save; shared USB hub between camera and capture drive is a known cause of loss — [forums.sharpcap.co.uk/viewtopic.php?p=9414](https://forums.sharpcap.co.uk/viewtopic.php?p=9414), [forums.sharpcap.co.uk/viewtopic.php?p=36527](https://forums.sharpcap.co.uk/viewtopic.php?p=36527)
- SharpCap has a **graphics acceleration** setting; one user reported crashes after a Remote Desktop session with it enabled — [forums.sharpcap.co.uk/viewtopic.php?p=50940](https://forums.sharpcap.co.uk/viewtopic.php?p=50940)
- High-DPI rendering was a known defect: *"you don't get the crisp text that you are used to with an HD display, instead it looks fluffy/blurry"* under Windows display scaling; developer said a planned UI update should fix it — [forums.sharpcap.co.uk/viewtopic.php?p=26602](https://forums.sharpcap.co.uk/viewtopic.php?p=26602) (thread titled "Upcoming Feature: New Dark Mode and UI improvements")
- Crash causes seen in official troubleshooting: *"some crashes have been caused by buggy Codecs installed by video camera driver packs"*, specifically naming 'Fast Picture Viewer Codec Pack' and 'Canon Hack Development Kit' — [www.sharpcap.co.uk/sharpcap/support-and-troubleshooting](https://www.sharpcap.co.uk/sharpcap/support-and-troubleshooting), [forums.sharpcap.co.uk/viewtopic.php?p=39090](https://forums.sharpcap.co.uk/viewtopic.php?p=39090)
- Reported crash signatures include `NullReferenceException` in `SharpCap.Models.Application.OnStopCapture()`, an ASCOM driver timeout, and a `BadImageFormatException` loading an image provider — [forums.sharpcap.co.uk/viewtopic.php?t=5](https://forums.sharpcap.co.uk/viewtopic.php?t=5)

### Inferences
- **Inferred (important, and explicitly inference):** No source — official or community — attributes SharpCap frame drops to **garbage collection or managed-runtime overhead**. Every documented drop cause is disk throughput, USB bandwidth, or CPU during save. For an engineering comparison this matters: the managed-runtime objection to SharpCap is **theoretically available but not empirically evidenced in the public record** I could reach. Do not claim GC-induced frame loss as a fact.
- **Inferred:** The existence of a "graphics acceleration" setting implies some display-path hardware acceleration (likely WPF/D3D or Direct2D for the preview), but there is **no evidence of GPU compute** (CUDA/OpenCL/compute shaders) for stacking, sharpening or debayer. Searches specifically for GPU acceleration returned nothing.
- **Inferred:** The high-DPI blurriness report is characteristic of a non-per-monitor-DPI-aware WinForms host being bitmap-scaled by Windows — consistent with the WinForms evidence in Q1.

### Gaps
- **No benchmark data of any kind** for SharpCap throughput, latency or CPU usage. No official performance figures, no community benchmark against FireCapture or any other app.
- No information on debayer implementation, preview-path cost at high fps, or whether preview is throttled independently of capture.
- No statement on thread-pool/affinity behaviour, or on whether capture runs at elevated priority.
- Whether the high-DPI issue is fixed in 4.1/4.2 — unknown.

---

## Q10. Practitioner criticism

### Takeaway
Independent, substantive criticism of SharpCap was **hard to find** — most reachable discussion is on SharpCap's own forum, where the developer is highly responsive, and the Cloudy Nights threads surfaced were positive. The concrete criticisms that do exist are: UI/high-DPI blurriness, high feature complexity requiring tutoring, crashes traced to third-party codecs and ASCOM drivers, multi-identical-camera profile conflicts, Smart Histogram/Brain producing bad gain recommendations on some cameras, and the developer-acknowledged SER timestamp imprecision. **Treat the absence of harsh criticism in my results as a sampling artefact of the blocked-fetch situation, not as evidence it does not exist.**

### Cited Findings
- Cloudy Nights reaction to 4.1 beta was positive per the developer: *"the 4.1 beta features are receiving generally positive feedback"* — [forums.sharpcap.co.uk/viewtopic.php?p=34268](https://forums.sharpcap.co.uk/viewtopic.php?p=34268); the relevant CN threads are [SharpCap 4.1 Now available](https://www.cloudynights.com/forums/topic/892670-sharpcap-41-now-available/), [Are you using SharpCap 4.1 yet? Does it make a difference?](https://www.cloudynights.com/forums/topic/891007-are-you-using-sharpcap-41-yet-does-it-make-a-difference/), and [SharpCap 4.2 Beta](https://www.cloudynights.com/forums/topic/1001878-sharpcap-42-beta/)
- **Complexity:** a forum thread titled "In need of a SharpCap tutor" indicates the learning curve is a real barrier — [forums.sharpcap.co.uk/viewtopic.php?p=6740](https://forums.sharpcap.co.uk/viewtopic.php?p=6740)
- **High-DPI / UI quality:** acknowledged by the developer (see Q9) — [forums.sharpcap.co.uk/viewtopic.php?p=26602](https://forums.sharpcap.co.uk/viewtopic.php?p=26602)
- **Startup crashes from third-party codec packs** — [forums.sharpcap.co.uk/viewtopic.php?p=39090](https://forums.sharpcap.co.uk/viewtopic.php?p=39090)
- **Crashes after Remote Desktop** with graphics acceleration enabled, thread still open — [forums.sharpcap.co.uk/viewtopic.php?p=50940](https://forums.sharpcap.co.uk/viewtopic.php?p=50940)
- **Mount connection errors**, especially over Wi-Fi; developer attributes some to unreliable wireless links and recommends wired testing — [forums.sharpcap.co.uk/viewtopic.php?p=50940](https://forums.sharpcap.co.uk/viewtopic.php?p=50940)
- **Dropped frames** are a recurring forum topic with its own pinned-style guide, "Understanding and Solving low frame rate issues and dropped frames when Solar/Lunar imaging" — [forums.sharpcap.co.uk/viewtopic.php?p=37019](https://forums.sharpcap.co.uk/viewtopic.php?p=37019); and a 2025-era thread "Frame Dropping Issue with ASI585MC in SharpCap – Possible USB Bottleneck?" — [forums.sharpcap.co.uk/viewtopic.php?p=49196](https://forums.sharpcap.co.uk/viewtopic.php?p=49196)
- **Smart Histogram defect report:** QHY168C gain set to 15 by the Brain, *"the whole image is saturated red"*, drivers *"never fixed"* — [forums.sharpcap.co.uk/viewtopic.php?p=18769](https://forums.sharpcap.co.uk/viewtopic.php?p=18769)
- **Polar-alignment failure in sparse fields** acknowledged in official guidance — [www.sharpcap.co.uk/?p=144](https://www.sharpcap.co.uk/?p=144)
- **Developer-acknowledged SER timestamp imprecision** (arrival-time, not exposure-corrected) — [forums.sharpcap.co.uk/viewtopic.php?p=37887](https://forums.sharpcap.co.uk/viewtopic.php?p=37887)
- **Competitive sentiment (anecdote, not measurement):** an AstroBin user *"switched to SharpCap because it handled everything they needed without the trouble they had with FireCapture"* — [app.astrobin.com/forum/topic/133391/asi676mc-and-firecapture](https://app.astrobin.com/forum/topic/133391/asi676mc-and-firecapture); a Stargazers Lounge user preferred SharpCap's cleaner look while noting both work well — [stargazerslounge.com/topic/381607-any-experiences-with-firecapture](https://stargazerslounge.com/topic/381607-any-experiences-with-firecapture)
- **Camera-support currency:** an AstroBin user found SharpCap recognised the ASI676MC while FireCapture 2.7.14 did not — [app.astrobin.com/forum/topic/133391/asi676mc-and-firecapture](https://app.astrobin.com/forum/topic/133391/asi676mc-and-firecapture)
- A claim that *"SharpCap produces heavily compressed MOV files unsuitable for planetary work"* appears on Stargazers Lounge but is labelled in-thread as **possibly a misconception** — [stargazerslounge.com/topic/381607-any-experiences-with-firecapture](https://stargazerslounge.com/topic/381607-any-experiences-with-firecapture). SharpCap's documented video formats are SER and AVI, not MOV, so this claim looks **false**; do not repeat it as criticism.
- Official bug/crash reporting process exists with structured guidance and log collection — [forums.sharpcap.co.uk/viewtopic.php?t=6](https://forums.sharpcap.co.uk/viewtopic.php?t=6), [forums.sharpcap.co.uk/viewtopic.php?t=5](https://forums.sharpcap.co.uk/viewtopic.php?t=5), [docs.sharpcap.co.uk/3.2/29_BugsCrashes.htm](https://docs.sharpcap.co.uk/3.2/29_BugsCrashes.htm)
- Developer triage policy: *"the developer will focus on the issues that are happening to more than one person"* — [www.sharpcap.co.uk/sharpcap/support-and-troubleshooting](https://www.sharpcap.co.uk/sharpcap/support-and-troubleshooting)

### Inferences
- **Inferred:** The criticism profile is that of a **mature, single-developer product with very high feature breadth**: the complaints are breadth-related (complexity, per-camera edge cases, third-party driver interactions) rather than core-capture-correctness. The frequency of "dropped frames" threads is notable but every reachable diagnosis attributes it to the user's I/O path, not to SharpCap.
- **Inferred:** Single-maintainer bus factor (Robin Glover) is a legitimate risk to note; the manual, forum moderation, releases and triage policy all point to one principal developer. No source states this explicitly as a risk.

### Gaps
- **No Reddit threads were retrieved at all** (r/AskAstrophotography, r/astrophotography). The search layer returned only SharpCap forum results for Reddit-targeted queries. This is a real gap in the practitioner-criticism coverage.
- **No Cloudy Nights thread critical of SharpCap was found.** I could identify three CN threads by title/URL but could not read their contents (fetch blocked), so I cannot characterise the discussion inside them beyond the developer's own summary.
- No head-to-head SharpCap-vs-FireCapture (or vs any other app) benchmark exists in anything I could reach — the only comparative material is anecdote.
- No independent review with measurements from a magazine or site.

---

## Summary table of hardest-confirmed facts

| Item | Value | Source |
|---|---|---|
| Language | C# | [User Manual](http://downloads.sharpcap.co.uk/docs/SharpCapUserManual_v40.pdf) |
| Runtime | .NET 8 (4.1); self-contained installer | [downloads page](https://www.sharpcap.co.uk/sharpcap/downloads), [4.1 install docs](https://docs.sharpcap.co.uk/4.1/3_InstallationandGettingStarted.htm) |
| UI framework | WinForms + WPF (evidence: crash stacks) | [forum crash logs](https://forums.sharpcap.co.uk/viewtopic.php?t=5) |
| Scripting | IronPython 3.4 (4.1+); no NumPy/SciPy/AstroPy | [4.0 scripting docs](https://docs.sharpcap.co.uk/4.0/33_Scripting.htm), [forum](https://forums.sharpcap.co.uk/viewtopic.php?p=33960) |
| Current stable | 4.1, GA 22 Sep 2023 | [Cloudy Nights](https://www.cloudynights.com/forums/topic/892670-sharpcap-41-now-available/) |
| Current beta | 4.2.15166.0 (64-bit), 21 Sep 2026 | [4.2 beta page](https://www.sharpcap.co.uk/sharpcap/sharpcap-downloads/sharpcap-4-2-beta) |
| Previous major | 4.0, June 2021 | [Cloudy Nights](https://www.cloudynights.com/forums/topic/892670-sharpcap-41-now-available/) |
| Pro price | £14/year personal (set April 2024) | [Pro FAQ](https://www.sharpcap.co.uk/?p=411), [forum](https://forums.sharpcap.co.uk/viewtopic.php?p=42457) |
| Pro feature count | "over 30" as of 4.1 | [news](https://www.sharpcap.co.uk/category/news) |
| Native camera families | ~14 named (Altair, AstCamPan, AstroAsis, Atik, Basler, Celestron, Imaging Source, Moravian, Player One, Point Grey, QHY, SVBony, Starlight Xpress, ZWO) | [4.0 requirements](https://docs.sharpcap.co.uk/4.0/3_Requirements.htm) |
| Video formats | SER, AVI | [4.0 capture docs](https://docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm) |
| Still formats | PNG, TIFF, FITS (and others) | [4.0 capture docs](https://docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm) |
| Auto-format threshold | 5 s exposure | [4.0 capture docs](https://docs.sharpcap.co.uk/4.0/11_CapturingVideosandImages.htm) |
| Polar align solver radius | 6–7° from pole (4.1); 5° earlier | [4.1 polar align docs](https://docs.sharpcap.co.uk/4.1/9_PolarAlignment.htm) |
| Live stack frame ceiling | ~32,000 frames (16-bit) | [forum](https://forums.sharpcap.co.uk/viewtopic.php?p=34918) |
| Platform | Windows only; 64-bit only from 2026 | [downloads page](https://www.sharpcap.co.uk/sharpcap/downloads), [forum](https://forums.sharpcap.co.uk/viewtopic.php?t=9286) |
| Multi-camera | Multiple processes only; no coordination | [forum](https://forums.sharpcap.co.uk/viewtopic.php?p=28505) |
