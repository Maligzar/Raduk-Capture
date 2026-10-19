# FireCapture — Technical Architecture and Implementation Profile

> **Research-method caveat (important for the report writer).** Direct HTTP fetches to `firecapture.de`, `cloudynights.com` and `archive.org` were **blocked at the network layer in this environment** (agent proxy returned `403` to `CONNECT www.firecapture.de:443`; DNS `ENOTFOUND` for the others). All content below that is attributed to those domains was obtained **indirectly**, via a search tool that reads and quotes the pages. Quoted strings are therefore second-hand transcriptions of those pages rather than my own direct reads. Where this matters for a load-bearing claim (notably the current version and its release date) I say so explicitly and give the conflict. Nothing here is from a first-party page I rendered myself.

---

## Implementation language, runtime and UI toolkit

### Takeaway
FireCapture is a **Java desktop application** distributed as `.jar` files with a **bundled JRE** and a set of **per-vendor native camera DLLs/shared objects** sitting alongside the Java code; this is confirmed from multiple independent angles (install layout, plugin API, vendor documentation). The specific **UI toolkit (Swing vs SWT vs JavaFX) could not be confirmed from any source** and is flagged as a gap.

### Cited Findings
- FireCapture's own documentation states Java no longer needs separate installation: "You don't need Java installed anymore because it's bundled with FireCapture." Windows setup is described as "Unpack the self-extracting folder from the download site." — [FireCapture configuration docs / download page, via search](https://www.firecapture.de/)
- A user report corroborates the bundled runtime: on first run "it will unpack a ton of files and thereafter use the java version it came bundled with." — [FireCapture download/Java discussion, via search](https://www.cloudynights.com/forums/topic/752781-firecapture-and-java-issue/)
- The application's Java code ships as a single jar. Users can update the program without reinstalling camera SDKs by replacing "the Firecapture.jar file in the existing lib folder" — i.e. the layout is `<install>/lib/Firecapture.jar`. — [firecapture@groups.io v2.7.15 thread, via search](https://groups.io/g/firecapture/topics)
- Native camera support is provided by **vendor SDK dynamic libraries placed in the main FireCapture folder** (not `lib/`), which users may swap by hand to pick up a newer camera. Named DLLs: ZWO `ASICamera2.dll`, QHY `qhyccd.dll`, Player One `PlayerOneCamera.dll`, SVBONY `SVBCameraSDK.dll`, Altair `altaircam.dll`, Touptek/Omegon `toupcam.dll`. — [Sky Inspector FireCapture Features Explainer](https://skyinspector.co.uk/firecapture-features-explainer/)
- Player One's own vendor instructions confirm the same mechanism: open the SDK's `lib` folder, take the `X64` subfolder on 64-bit Windows, and replace the matching DLL in the FireCapture folder. — [Player One Astronomy](https://player-one-astronomy.com/run-saturn-sqr-camera-in-current-firecapture-2-7-10/)
- Evidence of a thin, version-sensitive native wrapper layer: a Blackfly/Spinnaker user had to install the Spinnaker SDK with the **Visual Studio 2013** option because "the wrapper looked for `xxx_V120.dll` files" (`v120` = the MSVC 2013 toolset). — [firecapture@groups.io Spinnaker/Blackfly thread, via search](https://groups.io/g/firecapture/topic/spinnaker_camera_blackfly/78602279)
- Plugins are **Java `.jar` files**, written in Java, loaded from `<install>\plugins\x64\<PluginName>\` — see the Extensibility section. This is direct evidence the host process is a JVM. — [FlareDetect plugin manual (PDF)](https://www.joachim-stehle.de/downloads/FlareDetect/FlareDetectPluginManual.pdf); [Webastro "Ecrire son premier Plugin pour Firecapture en Java"](https://www.webastro.net/forums/topic/181484-ecrire-son-premier-plugin-pour-firecapture-en-java/)
- The JVM is launched with specific GC/heap flags: "FC starts Java with the `+UseCompressedOops` flag forcing it to use 32-bit references which results into 32GB max heap size." — [Cloudy Nights heap/memory thread, via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- A plugin failing to appear in the GUI was attributed to "a plugin ... compiled with a newer Java version than the one running on the system", indicating a conventional JVM class-file-version constraint between host and plugins. — [firecapture@groups.io plugin install thread, via search](https://groups.io/g/firecapture/topic/installing_plugins_in/104858363)

### Inferences
- **Confidence that FireCapture is a Java/JVM application: very high (effectively certain).** Four independent lines of evidence converge: a bundled JRE documented on the official site, a `lib/Firecapture.jar` artifact, Java `.jar` plugins with class-version compatibility problems, and documented JVM launch flags (`UseCompressedOops`).
- **Confidence that native camera SDKs are bridged via JNI (or a JNI-generating wrapper such as JNA/SWIG): high, but the mechanism is inferred, not documented.** The observable facts — vendor C DLLs loaded by a JVM process, a "wrapper" with an MSVC-toolset dependency, and per-architecture `x86`/`x64` separation — are exactly what a JNI bridge produces. No source states "JNI" in words. Treat the *existence* of a native bridge as fact and the *specific* binding technology as inference.
- The per-architecture plugin directories (`plugins\x86`, `plugins\x64`) imply plugins themselves may carry or depend on native code, not just pure Java.
- The bundled-JRE model means FireCapture controls its own Java version, which removes end-user "install Java" friction but also means the shipped JVM is whatever the author last packaged.

### Gaps
- **UI toolkit is unconfirmed.** No source found states Swing, SWT, or JavaFX. The brief's premise ("Swing or other toolkit") could not be verified either way. Do not assert Swing in the report without a direct look at the installed `lib/` contents or a decompile.
- **Exact bundled Java version is unknown** (e.g. Java 8 vs 11 vs 17). The only hint is the plugin class-version complaint, which implies the bundled JRE is older than some contributors' compilers.
- Whether the bridge is hand-written JNI, JNA, SWIG, or BridJ is unestablished.
- The build/packaging toolchain is unknown.

---

## Why Java? — the author's rationale and its engineering consequences

### Takeaway
**No statement by Torsten Edelmann explaining the choice of Java was found.** The benefit the community consistently cites is cross-platform reach; the costs the community consistently reports are heap tuning, memory exhaustion, and multithreading weaknesses.

### Cited Findings
- The cross-platform payoff is the reason users give: FireCapture "continued to be multi-platform (written in Java) and free", while SharpCap is Windows-only. — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- A concrete threading criticism from a practitioner: "Firecapture can't really be used for DSO photography because the GUI or camera control isn't that well multi-threaded." — [Cloudy Nights, Sharpcap vs firecapture](https://www.cloudynights.com/forums/topic/901861-sharpcap-vs-firecapture/)
- The author did add parallelism over time: FireCapture v2.7 release notes record the addition of "multicore support and threaded histogram calculation", with multicore support exposed as a user setting in system/general settings. — [FireCapture ReleaseNotes.pdf, via search](https://www.firecapture.de/ReleaseNotes.pdf)
- Users must manage the Java heap by hand — a direct, user-visible consequence of the JVM: on 64-bit builds "the heap can be set in General settings"; rules of thumb circulated are "around half the physical RAM you have" or "25% of RAM". — [Cloudy Nights heap thread, via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- A JVM-specific allocation ceiling bites users: a 32 GB machine "would not start with a 22 GB heap, because Java could not reserve that much memory", attributed to the `UseCompressedOops` 32 GB reference ceiling. — [Cloudy Nights heap thread, via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- The author's own stated mitigation for allocation churn during capture: "In RAM buffer mode FC pre-allocates frame buffers so the memory doesn't need to be allocated/released all the time." — [Cloudy Nights / groups.io, developer advice, via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- There is an explicit GC-pressure escape hatch in the UI: a "Force aggressive memory recovery during capture" option under Misc performance settings, which one user said "helped performance tremendously" for erratic captures. — [Cloudy Nights, Firecapture erratic behavior during capture](https://cloudynights.com/topic/614351-firecapture-erratic-behavior-during-capture)
- The legacy 32-bit line was heap-constrained in the classic Java way: "Under 32bit Java can allocate 1300-1500 MB at the most. A much better solution would be switching to a x64 system." — [Cloudy Nights, via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)

### Inferences
- The existence of a **pre-allocated RAM buffer mode** and a **"force aggressive memory recovery" toggle** is strong circumstantial evidence that GC behaviour during high-frame-rate capture is a real, acknowledged engineering problem that the author designed around rather than one that the JVM solved for free. This is the single most citable architectural consequence of the Java choice for an engineering comparison.
- Exposing Java heap size as an end-user setting is an architectural tell: a native Win32/C++ competitor has no equivalent knob to get wrong, so this is a genuine usability/support-cost differentiator.
- The 32 GB `UseCompressedOops` ceiling is a hard architectural cap on in-RAM capture buffer size that a 64-bit native application does not share.

### Gaps
- **No primary-source statement from Torsten Edelmann on why he chose Java.** Searched the official site, groups.io and Cloudy Nights; nothing found. Do not fabricate a rationale.
- **No measured GC pause data** (no benchmark, no frame-drop-vs-pause correlation) exists in any source found. The GC story is qualitative only.
- **No data on startup time** was found.
- **No sourced characterisation of "UI feel"** beyond general "dated UI" sentiment (see Reputation section); nothing ties UI feel specifically to the toolkit.

---

## Current version, release cadence, and whether the project is actively maintained (as of October 2026)

### Takeaway
The current release is **v2.7.15**, the fifteenth point release of a 2.7 line that began in 2020–2022. **Its release date is genuinely contested across sources (December 2025 vs March 2025 vs February 2026)** and I could not resolve it. **No v3.x exists.** **No release or developer announcement dated anywhere in 2026 was found** — the project reads as low-cadence and possibly dormant, but I cannot assert abandonment.

### Cited Findings
- v2.7.15 is the current build. The official download table lists "FC v2.7.15 (x64)" as current, alongside legacy 2.6 and 2.5 downloads. — [firecapture.de homepage, via search](https://www.firecapture.de/)
- **Date conflict #1:** a groups.io release listing gives "v2.7.15 (03/12/2025)" — day-first European formatting, i.e. **3 December 2025**, consistent with the author's other European-style dates. — [firecapture@groups.io, via search](https://groups.io/g/firecapture/topics)
- **Date conflict #2:** the official homepage's news list places the v2.7.15 announcement near a **"March 2025"** heading. — [firecapture.de homepage, via search](https://www.firecapture.de/)
- **Date conflict #3:** a third-party Mac software catalogue lists "Version: 2.7.15" with a first-release date of **February 23, 2026**. This is an aggregator and is the weakest of the three. — [astrowhat.com FireCapture (Mac)](https://astrowhat.com/resources/firecapture-mac.521/)
- A third-party features explainer, **last updated July 2025**, refers to "Beta 2.7.15 on 12-3-2025" as the latest it covers. — [Sky Inspector FireCapture Features Explainer](https://skyinspector.co.uk/firecapture-features-explainer/)
- v2.7.15's changes are **mostly SDK refreshes, not new architecture**: "SDK updates: ZWO, QHY, PlayerOne, Touptek, Altair, OGMA, plus a few new options and fixes." — [firecapture@groups.io v2.7.15 release notes, via search](https://groups.io/g/firecapture/topics)
- Prior release for cadence anchoring: **v2.7.14 announced 4 December 2023**, Windows first, "Mac/Linux/Raspberry versions ... available later that week". — [firecapture@groups.io v2.7.14 thread](https://groups.io/g/firecapture/topic/v2_7_14/102966557)
- v2.7.14's headline feature was a sharpening tool. — [Cloudy Nights, "Firecapture 2.7.14 - Now with sharpening!"](https://www.cloudynights.com/forums/topic/903782-firecapture-2714-now-with-sharpening/)
- The 2.7 line's origins: "FireCapture v2.7 has been released!" is dated **March 2022** on the official news list, and the **2.7 beta dates to roughly 2020** (Cloudy Nights announcement thread). — [firecapture.de homepage, via search](https://www.firecapture.de/); [Cloudy Nights, FireCapture 2.7 Beta available](https://www.cloudynights.com/forums/topic/729020-firecapture-27-beta-avaiable-for-download/)
- **No 2026 announcement found** on the groups.io list; a targeted domain-restricted search of groups.io/cloudynights/firecapture.de surfaced nothing dated 2026. — [firecapture@groups.io topic index, via search](https://groups.io/g/firecapture/topics)
- **No v3.x found.** A targeted search for a v3 beta returned nothing; the only "2.7 beta" thread is ~6 years old and a commenter there said they "don't think there's a 'new' 2.7beta yet." — [Cloudy Nights, FireCapture 2.7 Beta thread](https://www.cloudynights.com/forums/topic/729020-firecapture-27-beta-avaiable-for-download/)
- A beta channel page exists at `firecapture.de/beta.html`, but I could not read its contents. — [firecapture.de/beta.html (URL surfaced in search, content not retrieved)](https://firecapture.de/beta.html)
- Evidence of continuing *vendor* engagement with the author: "Player One has released the new SDK for Player One cameras, and it was sent to Torsten to implement it in the next Firecapture update." Thread date unclear. — [firecapture@groups.io, via search](https://groups.io/g/firecapture/topics)
- **Likely false positive to discard:** the Player One software page lists "V3.10.1 ... 2026/05/01" with Windows/Linux/macOS/Raspberry Pi rows. On that same page the FireCapture statement is only "Firecapture V2.7 and later version is supported (Windows, Mac and Linux)". The V3.10.1 is almost certainly Player One's own driver/SDK version, **not a FireCapture version.** — [Player One Astronomy software page](https://player-one-astronomy.com/service/software/)

### Inferences
- **Best estimate of the most recent release: v2.7.15, around December 2025** (the groups.io developer-channel date is the strongest source and reads naturally as DD/MM/YYYY; the "12-3-2025" in the Sky Inspector explainer is plausibly the same date written 12 March vs 3 December, which is precisely the ambiguity). **Flag this in the report as unresolved.**
- **Release cadence is roughly annual to biennial** for the 2.7 line: 2.7 beta ~2020 → 2.7 final March 2022 → 2.7.14 December 2023 → 2.7.15 ~2025. That is ~24 months between the last two point releases.
- **Maintenance status as of October 2026: low-activity but not demonstrably abandoned.** The content of 2.7.15 (SDK refreshes) is maintenance work, which is what a part-time solo maintainer keeping up with camera vendors produces. Absence of a 2026 release is consistent with the established ~2-year cadence and does not by itself prove abandonment — but **~10–22 months with no verifiable release or author communication is a legitimate concern to raise in a comparison.**
- The architectural line is **frozen**: a 2.7 line running since 2020 with no v3 means there has been no major re-platforming in ~6 years.

### Gaps
- **I could not verify any FireCapture release, forum post, or author communication dated in 2026.** This is a material gap for the brief's central question. It may reflect my inability to reach `firecapture.de` and the groups.io archive directly rather than genuine silence. **The report should state this as "no 2026 activity verifiable from here", not "no 2026 activity".**
- The exact v2.7.15 release date is unresolved (three conflicting candidates spanning March 2025 – February 2026).
- Contents of `firecapture.de/beta.html` unknown — a newer beta could exist there.
- No information on whether Torsten Edelmann has made any public statement about the project's future.

---

## Licensing, cost and source availability

### Takeaway
Freeware for private use with an explicit **commercial-use prohibition**, funded by voluntary donations, **no paid tier**, and **closed source** by strong implication (reverse engineering and derivative works are expressly forbidden).

### Cited Findings
- License grant: "Permission is granted to anyone to use this software for private purpose. Commercial use of this software is prohibited." — [FireCapture License](https://www.firecapture.de/license.html)
- Source/derivative restrictions: "You shall not reverse engineer, decompile, disassemble or create any kind of derivative works based on the Software." — [FireCapture License](https://www.firecapture.de/license.html)
- Redistribution restriction: "You may not redistribute or alter this software without the written permission of the author." — [FireCapture License](https://www.firecapture.de/license.html)
- Donationware model, no obligation: "If you find FireCapture useful and would like to support me in continuing development you're welcome to donate a small amount of your choice." — [firecapture.de, via search](https://www.firecapture.de/)
- Plugins are separately owned: community plugins are "copyrighted by their respective authors." — [FireCapture License](https://www.firecapture.de/license.html)
- The price point is the most-cited contrast with SharpCap in community threads: a license charge was introduced for SharpCap, while FireCapture remained free. — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- Third-party catalogues classify it as "proprietary but free". — [AlternativeTo FireCapture](https://alternativeto.net/software/firecapture/about)

### Inferences
- **"Closed source" is an inference, not a quoted statement.** No source says "the source code is not available". The anti-decompilation and anti-derivative clauses plus the absence of any public repository make closed-source the only consistent reading, and I rate confidence high — but label it as inferred.
- The commercial-use prohibition is a concrete, citable differentiator: FireCapture is **not** licensable for e.g. an observatory operating commercially or a reseller bundling it, without written permission from the author.
- Donationware + solo author is the structural root of the cadence and bus-factor concerns above.

### Gaps
- No data on donation volume, any Patreon/sponsorship, or whether a commercial license can in practice be purchased.
- No published source repository was found; absence of evidence only.

---

## Platform and architecture support

### Takeaway
Windows, macOS, Linux (x86-64) and Linux aarch64 (Raspberry Pi) builds exist; the line is **64-bit only**, with x86 explicitly declined by the author. **Native Apple Silicon (arm64) status is unconfirmed.**

### Cited Findings
- Official download table lists v2.7.15 builds for **x64 and aarch64 (Raspberry Pi)**, plus a Fedora PPA option. — [firecapture.de homepage, via search](https://www.firecapture.de/)
- Author declined 32-bit Windows at the 2.7 beta stage: the beta shipped as "FC v2.7beta x64 (Win)" and the developer stated "I currently don't have plans to provide a Win x86 version" (statement ~2020). — [firecapture@groups.io FC v2.7beta thread](https://groups.io/g/firecapture/topic/fc_v2_7beta/75005490)
- Raspberry Pi distribution is a Debian package: a March 2024 Raspberry Pi 5 install guide used `firecapture_2.7.14_aarch64.deb`. — [astroisk.nl, Installing FireCapture on Raspberry Pi 5 (2024-03-24)](https://astroisk.nl/blog/2024/03/24/imaging-the-solar-system-installing-firecapture-on-raspberry-pi-5/)
- v2.7 added **INDI support for Mac and Linux**, the cross-platform device-control path. — [FireCapture ReleaseNotes.pdf, via search](https://www.firecapture.de/ReleaseNotes.pdf)
- Apple Silicon: community posts from 2021 report FireCapture running on M1 Macs, but one user "was not able to reach the same FPS than of Windows" on Mac (Intel or M1); a later user reported better frame rates on a newer MacBook Air at full resolution. — [firecapture@groups.io, FC on MacBook with M1 processor possible?](https://groups.io/g/firecapture/topic/fc_on_macbook_with_m1/85452691)
- Non-Pi ARM: a user ran the aarch64 build on an **ODROID M1** board successfully; a Raspberry Pi 4 was noted as having limited USB 3 bandwidth. — [firecapture@groups.io, new version for Raspberry thread](https://groups.io/g/firecapture/topic/new_version_for_raspberry/101209469?page=3)
- Platform parity is imperfect for devices: a QHY camera on a Pi 4 would "always crash when loading QHY driver". — [firecapture@groups.io, FC on RPI4 and QHY camera support?](https://groups.io/g/firecapture/topic/80308825)
- A Stellarmate/INDI user reported that on that platform "Only ZWO ASI connects". — [INDI forum](https://indilib.org/forum/stellarmate/13918-firecapture-on-sm-x-only-zwo-asi-connects.html)

### Inferences
- **Cross-platform reach is FireCapture's strongest architectural differentiator** against a Windows-only Win32/C++ competitor, and the Java runtime is the enabling reason — but the reach is **uneven in practice**, because the camera layer is native per-vendor code and therefore does not port for free. The QHY-on-Pi crash and the Stellarmate ZWO-only report are concrete demonstrations that the native bridge, not the Java UI, is where portability breaks down. This is the key architectural point for the comparison.
- Mac Apple Silicon support is presumably at least functional under Rosetta or natively, but **whether the shipped Mac binary is a native arm64 build is unverified**; the 2021 FPS deficit reports predate current releases and should not be cited as current.

### Gaps
- No confirmation that the macOS build is a native arm64 binary vs x86-64 under Rosetta 2.
- macOS minimum version unknown.
- Which Linux distributions/glibc versions are supported is unknown beyond the Fedora PPA mention and Debian `.deb`.
- No confirmation whether the Windows x86 (32-bit) line is formally discontinued or merely not updated (the author's statement is from ~2020, and `plugins\x86` directories still appear in plugin documentation).

---

## Hardware support: cameras, mounts, focusers, filter wheels, ADC

### Takeaway
A broad per-vendor native camera layer — ZWO, QHY, Player One, Touptek/Omegon, Altair, OGMA, SVBony, Basler, plus ASCOM and INDI generic paths — with the SDKs shipped as swappable native libraries. Support quality is explicitly uneven, and the author has stated he cannot test some interfaces.

### Cited Findings
- The official site presents a compatibility matrix listing **ASCOM, Basler, OGMA, PlayerOne, QHY, Touptek/Omegon, and ZWO ASI** under a "wide range of supported cameras" heading. (The matrix's check marks did not come through in the retrieved text, so per-feature-per-vendor detail is unavailable.) — [firecapture.de homepage, via search](https://www.firecapture.de/)
- v2.7.15 refreshed SDKs for **ZWO, QHY, PlayerOne, Touptek, Altair, OGMA**. — [firecapture@groups.io v2.7.15 release notes, via search](https://groups.io/g/firecapture/topics)
- The 2.7 beta added **Touptek and SVBony** camera support. — [firecapture@groups.io FC v2.7beta](https://groups.io/g/firecapture/topic/fc_v2_7beta/75005490)
- **Basler support is explicitly unvalidated by the author:** "the Basler interface is completely untested and might have major issues because I don't have a Basler camera for testing." — [FireCapture ReleaseNotes.pdf, via search](https://www.firecapture.de/ReleaseNotes.pdf)
- **PointGrey/FLIR (Spinnaker)** is supported at least to the extent that a Blackfly user got it working after installing Spinnaker with the VS2013 options. — [firecapture@groups.io Spinnaker/Blackfly thread](https://groups.io/g/firecapture/topic/spinnaker_camera_blackfly/78602279)
- **IDS and TIS/Skyris and PGR** appear in the older v2.5 beta interface list: "x64 version is available for the following camera interfaces: ASI, PGR, Basler, TIS/Skyris, IDS." **This is a v2.5-era statement and may not hold for 2.7.15.** — [firecapture.de v2.5 beta page, via search](https://www.firecapture.de/)
- Vendor DLL names confirm the per-vendor native integration: ZWO `ASICamera2.dll`, QHY `qhyccd.dll`, PlayerOne `PlayerOneCamera.dll`, SVBONY `SVBCameraSDK.dll`, Altair `altaircam.dll`, Touptek/Omegon `toupcam.dll`; a new camera may additionally need the vendor's driver installed. — [Sky Inspector FireCapture Features Explainer](https://skyinspector.co.uk/firecapture-features-explainer/)
- **Mount control / autoguiding interfaces** are enumerated in the settings documentation: "ASCOM", "Cookbook CCD serial interface (by Richard Berry)", and "Camera-internal ST4 port". — [WonderPlanets FireCapture Settings Panel docs](http://www.wonderplanets.de/FireCapture/help/settings.html)
- **INDI** is the mount/device path on Mac and Linux, added in v2.7. — [FireCapture ReleaseNotes.pdf, via search](https://www.firecapture.de/ReleaseNotes.pdf)
- **ADC tuning is a named, differentiating feature:** FireCapture "has tools to help you precisely tune an atmospheric dispersion compensator". — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- **Focuser control** exists at least via the community autofocus plugin covering Moon, Sun and Planets. — [GitHub: Spartano78/Firecapture-Autofocus-Plugin](https://github.com/Spartano78/Firecapture-Autofocus-Plugin)

### Inferences
- The camera layer is a **set of per-vendor native wrappers, each as current as the last time the author rebuilt against that vendor's SDK**. This is why point releases are dominated by "SDK updates" and why users are told to hand-swap a DLL to run a camera newer than the FireCapture build. For a competitor comparison: FireCapture's breadth is real but its support latency for brand-new camera models is mediated by a single maintainer's SDK refresh cycle, with a documented user workaround.
- The author's own "completely untested" admission about Basler is the clearest evidence that the long vendor list overstates uniform support quality.

### Gaps
- **No camera count.** No source gives a number of supported models.
- Current status of **IDS, The Imaging Source (DMK), Celestron, and generic webcam/DirectShow** support in 2.7.15 is unconfirmed — only the v2.5-era list and the official matrix (whose cell contents I could not read) touch these.
- **Filter wheel control was not confirmed by any source.** Do not assert it.
- Whether focuser control is native or plugin-only is unclear.

---

## Capture pipeline, frame rates and output formats

### Takeaway
A buffered pipeline with an optional **pre-allocated RAM buffer mode**, ROI capture, and SER/AVI/FIT/TIF outputs with WinJUPOS-compatible filename timestamping. Dropped frames are a well-documented user-facing failure mode traced to disk, buffer, USB and bit-depth bottlenecks.

### Cited Findings
- **RAM buffer mode pre-allocates frame buffers**, per the developer, "so the memory doesn't need to be allocated/released all the time". — [Cloudy Nights heap thread, developer advice via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- RAM buffer is also reported as the more robust path: "the RAM buffer setting in FireCapture — it seems to be more stable and higher performance than the standard buffering method." — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- Heap and RAM buffer compete for the same JVM memory: the heap "dictates the size of the java heap ('user memory'). Things like the RAM buffer detract from that available memory." — [Cloudy Nights heap thread, via search](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- **Dropped-frame mechanism:** when the disk cannot keep up, frames accumulate in memory "until the buffer is full, and then frames are dropped"; the recommended diagnosis order is "SSD -> memory size -> settings for memory -> camera settings". — [Cloudy Nights, via search](https://www.cloudynights.com/forums/topic/781236-firecapture-and-fps/)
- Not all drops are memory-related: "lost frames aren't necessarily associated with the RAM buffer being full. More typically, there's a communication issue between the camera and the computer", with long or active **USB3 extension cables** and **16-bit mode** named as causes. — [Cloudy Nights, via search](https://www.cloudynights.com/forums/topic/781236-firecapture-and-fps/)
- A "Force aggressive memory recovery during capture" option exists under Misc performance settings and fixed erratic capture for at least one user. — [Cloudy Nights, Firecapture erratic behavior during capture](https://cloudynights.com/topic/614351-firecapture-erratic-behavior-during-capture)
- **Output formats are bit-depth-dependent.** Per the Capture panel documentation: "if the camera supports 12-bit effective data and the 16bit checkbox is enabled, the supported formats are FIT, TIF and SER." — [WonderPlanets FireCapture Capture Panel docs](http://www.wonderplanets.de/FireCapture/help/capture.html)
- Consistent user experience of that constraint in 2.6: "there seems to be no way to actually record an AVI. The only options available to me are SER for video and FIT and TIF for imaging" — and a separate user got AVI back by changing the camera's bit-depth setting. — [Cloudy Nights, FireCapture 2.6 AVI option](https://www.cloudynights.com/forums/topic/622510-firecapture-26-avi-option/)
- **SER is the preferred container** in practice: SER output is uncompressed, so converting to AVI "would result in lossy compression"; and "WinJUPOS doesn't seem to like AVIs much greater than 2GB." — [Cloudy Nights, Converting SER to AVI or MPEG](https://www.cloudynights.com/forums/topic/536678-converting-ser-to-avi-or-mpeg/)
- **WinJUPOS filename timestamping is a built-in option** under Settings > Capture Settings, producing filenames from which WinJUPOS reads date and time. — [Cloudy Nights, Help with WinJupos (De-Rotation)](https://www.cloudynights.com/forums/topic/531274-help-with-winjupos-de-rotation-autostakkeret/); [rk.edu.pl, Features of FireCapture 2.2](https://rk.edu.pl/en/features-firecapture-22/)
- SER files written with embedded timestamps are what downstream tools require for de-rotation workflows (PIPP: "the input files must be SER files containing timestamps..."). — [PIPP output options documentation](https://sites.google.com/site/astropipp/pipp-manual/output-options)
- **ROI handling:** ROI is user-settable and movable with keyboard shortcuts; a smaller ROI raises FPS but risks losing the target, and a larger ROI means "larger capture files, potentially slower FPS". — [Cloudy Nights, Autoalign in Firecapture](https://www.cloudynights.com/topic/795180-autoalign-in-firecapture/)
- v2.7 added "multicore support and threaded histogram calculation". — [FireCapture ReleaseNotes.pdf, via search](https://www.firecapture.de/ReleaseNotes.pdf)

### Inferences
- The pipeline shape is: camera SDK callback → Java-side frame buffer (optionally pre-allocated pool) → disk writer. The pre-allocated pool exists specifically to keep the allocator and GC out of the per-frame path, which is the canonical Java-for-realtime mitigation.
- **The JVM heap ceiling caps RAM-capture duration.** With `UseCompressedOops` capping the heap near 32 GB and the RAM buffer drawing from that heap, maximum in-RAM capture length is architecturally bounded in a way a 64-bit native app's is not. This is a sharp, defensible comparison point — though note it is assembled from two separately-sourced facts, so present the *combination* as inference.
- PNG does not appear in any source's format list; the confirmed set is **SER, AVI, FIT, TIF**.

### Gaps
- **No achievable frame-rate figures of any kind were found** — no fps numbers for any camera/ROI/bit-depth combination, from either the vendor or the community. The brief asked for concrete numbers; there are none I can honestly supply.
- No sourced detail on the disk write path (buffered vs direct I/O, writer thread count, queue depth).
- **PNG output unconfirmed** — do not list it.
- Whether SER files carry per-frame embedded timestamps (as opposed to only WinJUPOS filename timestamps) was not directly confirmed for FireCapture.
- No sourced detail on how dropped frames are reported to the user or recorded in logs.

---

## Planetary-specific features

### Takeaway
FireCapture's reputation rests on planetary-specific automation — AutoAlign (planet centring), AutoGuide (mount correction from the main camera, no guide scope), ADC tuning aids, and WinJUPOS integration. Several features in the brief could not be confirmed.

### Cited Findings
- **AutoAlign** centres the planet in each frame before saving, which helps downstream stacking. It operates in three distinct modes: "Centroid stabilization within the ROI, updating of the ROI location when the planet gets near the edge of the ROI, and guiding of the mount". Importantly, in the basic mode "it shifts the target within the ROI but does not move the mount", so a small ROI can still drift off target. — [Cloudy Nights, Autoalign in Firecapture](https://www.cloudynights.com/topic/795180-autoalign-in-firecapture/); [Cloudy Nights, FireCapture Auto-Align Question](https://www.cloudynights.com/forums/topic/629603-firecapture-auto-align-question/)
- **AutoGuide** sends corrections to the mount using the main capture camera's frames, **with no separate guide camera**, over ASCOM, the Cookbook CCD serial interface, or the camera's internal ST4 port. — [WonderPlanets FireCapture Settings Panel docs](http://www.wonderplanets.de/FireCapture/help/settings.html)
- Practitioner framing of the combined value: FireCapture "has tools to help you precisely tune an atmospheric dispersion compensator, can auto guide and auto align on planets and lunar features as well." — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- Autoguiding is the recommended answer to ROI drift: if the mount can't hold the planet in the ROI, "either use a larger ROI ... or ... enable FireCapture's rudimentary (but effective, nonetheless) autoguiding capability." — [Cloudy Nights, Autoalign in Firecapture](https://www.cloudynights.com/topic/795180-autoalign-in-firecapture/)
- **Real-time sharpening** was added in v2.7.14 (December 2023). — [Cloudy Nights, "Firecapture 2.7.14 - Now with sharpening!"](https://www.cloudynights.com/forums/topic/903782-firecapture-2714-now-with-sharpening/)
- **WinJUPOS-compatible filenames** for de-rotation workflows are a built-in capture setting. — [Cloudy Nights, Help with WinJupos (De-Rotation)](https://www.cloudynights.com/forums/topic/531274-help-with-winjupos-de-rotation-autostakkeret/)
- Deep-sky is explicitly out of scope: "FireCapture cannot track or align deep-sky objects"; SharpCap's live stacking "blows FireCapture away". — [Cloudy Nights, Sharpcap vs firecapture](https://www.cloudynights.com/forums/topic/901861-sharpcap-vs-firecapture/)
- A practitioner who uses both: "I use FireCapture for all acquisition for solar system subjects." — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)

### Inferences
- AutoAlign's three-mode structure (intra-ROI centroid stabilisation / ROI repositioning / mount guiding) is a well-designed escalation ladder and is the feature most responsible for FireCapture's planetary reputation. The clarification that the base mode does not move the mount is a frequent source of user confusion and worth noting.
- Pairing AutoGuide-off-the-main-camera with WinJUPOS filename output means FireCapture is architected end-to-end around the de-rotation workflow, which is the planetary imager's actual pipeline.

### Gaps
- **Histogram-based auto-exposure: not confirmed.** v2.7 added "threaded histogram calculation" (release notes) but I found no source describing an auto-exposure feature driven by it.
- **Quality estimation / lucky-imaging frame scoring: not confirmed by any source.** Do not assert.
- **Limb detection: not confirmed.**
- **Focus aids (beyond the third-party autofocus plugin): not confirmed.**
- **The "profile system": not confirmed by any source**, though v2.7 is documented to prompt for "copying settings from an older installation on first launch" — [FireCapture ReleaseNotes.pdf, via search](https://www.firecapture.de/ReleaseNotes.pdf).
- **Batch / scheduled capture: not confirmed.**
- **Named Jupiter / Saturn / Mars / Lunar / Solar object modes: not confirmed**, although lunar and solar use are widely attested in forum threads.
- **Built-in de-rotation: not found** — the de-rotation story is WinJUPOS integration via filenames, i.e. FireCapture feeds an external tool rather than de-rotating itself.

---

## Extensibility: the plugin architecture

### Takeaway
A **Java plugin API introduced in v2.5**; plugins are `.jar` files, written in Java, each in its own subfolder under a per-architecture plugins directory, with access to image data.

### Cited Findings
- "Starting with version v2.5 FireCapture comes with a built-in plugin interface giving access to image data and the possibility for extending functionality." — [firecapture.de, via search](https://firecapture.de/index.html)
- Plugins are Java; a community tutorial is titled "Ecrire son premier Plugin pour Firecapture en Java". — [Webastro](https://www.webastro.net/forums/topic/181484-ecrire-son-premier-plugin-pour-firecapture-en-java/)
- Install layout: create a folder for the plugin "in folder `<FC installation folder>\plugins\x86` or `<FC installation folder>\plugins\x64`", then copy the jar into it. **Each jar must sit in its own subfolder** — placing jars directly in `x64` is a documented failure mode. — [FlareDetect plugin manual (PDF)](https://www.joachim-stehle.de/downloads/FlareDetect/FlareDetectPluginManual.pdf); [firecapture@groups.io plugin install thread](https://groups.io/g/firecapture/topic/installing_plugins_in/104858363)
- Plugins may ship dependencies: the SSSMon plugin instructs copying its bundled jars into "the lib folder of the FC installation folder"; a developer guide mentions adding an **OpenCV jar** to the plugin's folder. — [SSSMon plugin manual (PDF)](http://www.joachim-stehle.de/downloads/SSSMon/SSSMonPluginManual.pdf)
- Plugins load at startup and appear in the **pre-processing panel**. — [firecapture@groups.io plugin install thread](https://groups.io/g/firecapture/topic/installing_plugins_in/104858363)
- Plugins are copyrighted by their respective authors, i.e. third parties may license their plugins independently. — [FireCapture License](https://www.firecapture.de/license.html)
- **Notable plugins found:**
  - *Firecapture-Autofocus-Plugin* — free autofocus for Moon, Sun and Planets, 12 languages, open on GitHub. — [GitHub: Spartano78/Firecapture-Autofocus-Plugin](https://github.com/Spartano78/Firecapture-Autofocus-Plugin)
  - *SSSMon* — Solar Scintillation Seeing Monitor plugin, user manual v0.92. — [SSSMon manual (PDF)](http://www.joachim-stehle.de/downloads/SSSMon/SSSMonPluginManual.pdf)
  - *FlareDetect* — solar flare detection plugin, manual v0.90. — [FlareDetect manual (PDF)](https://www.joachim-stehle.de/downloads/FlareDetect/FlareDetectPluginManual.pdf)
  - *MoonPanoramaMaker* — lunar mosaic automation that drives FireCapture. — [GitHub: Rolf-Hempel/MoonPanoramaMaker](https://github.com/Rolf-Hempel/MoonPanoramaMaker/releases)

### Inferences
- **Writing a FireCapture plugin requires a Java toolchain**, and plugins must be compiled against a Java version no newer than FireCapture's bundled JRE (per the reported class-version failure). That is a real friction point for contributors and a notable contrast with a native app offering a C ABI or scripting interface.
- The plugin surface appears focused on **image-data pre-processing** rather than full application scripting, based on where plugins surface in the UI and what the three documented plugins do. This is inference from three examples, not from an API spec.
- The ecosystem is small but genuine — a handful of actively documented third-party plugins over roughly a decade.

### Gaps
- **No published plugin SDK documentation was found.** No API reference, no interface/class names, no Javadoc. Community tutorials and plugin READMEs are the only guidance located.
- No plugin registry or count.
- Whether plugins can touch capture control (not just image data) is unknown.

---

## Practitioner reputation and criticism

### Takeaway
The consensus among experienced imagers is **FireCapture for solar-system acquisition, SharpCap for deep-sky and all-round use**. Its cited strengths are planetary automation, cross-platform reach and zero cost; its cited weaknesses are weak multithreading, memory/buffer tuning burden, and capture instability during long high-frame-rate runs.

### Cited Findings
- Clear domain split: "I use FireCapture for all acquisition for solar system subjects." — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- Threading criticism: "Firecapture can't really be used for DSO photography because the GUI or camera control isn't that well multi-threaded." — [Cloudy Nights, Sharpcap vs firecapture](https://www.cloudynights.com/forums/topic/901861-sharpcap-vs-firecapture/)
- Feature gap: "Live stacking in SharpCap blows FireCapture away"; "FireCapture cannot track or align deep-sky objects." — [Cloudy Nights, Sharpcap vs firecapture](https://www.cloudynights.com/forums/topic/901861-sharpcap-vs-firecapture/)
- Ease of use is contested, not one-sided: some find FireCapture simpler once configured; others say "I still find SharpCap easier to use." — [Cloudy Nights, Sharpcap vs firecapture](https://www.cloudynights.com/forums/topic/901861-sharpcap-vs-firecapture/)
- **Stability during long high-frame-rate runs is a recurring complaint**, with buffering problems reported even to fast SSDs; the RAM buffer is the community's standard workaround. — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- **Java friction is real but has diminished.** A 2023 IceInSpace thread records a user hitting a Java error on Windows 10 with FireCapture 2.7, the program flashing open and closed; the thread does not record a resolution. Current builds bundle the JRE. — [IceInSpace forum thread (Oct 2023)](https://www.iceinspace.com.au/forum/showthread.php?p=1593732); [Cloudy Nights, Firecapture and Java issue](https://www.cloudynights.com/forums/topic/752781-firecapture-and-java-issue/)
- Memory exhaustion is common enough to have its own long-running thread, titled "What's the magic combination of settings to get FireCapture to not run out of memory?" — [Cloudy Nights](https://www.cloudynights.com/forums/topic/785873-whats-the-magic-combination-of-settings-to-get-firecapture-to-not-run-out-of-memory/)
- Cost and platform are the standard pro-FireCapture arguments: SharpCap introduced a license charge while FireCapture remained free and multi-platform. — [Cloudy Nights, FireCapture vs. SharpCap Pro](https://www.cloudynights.com/forums/topic/789597-firecapture-vs-sharpcap-pro/)
- Donation goodwill is notable: one user reported sending a $50 donation because the author "graciously makes the app free to the community." — [Cloudy Nights, via search](https://www.cloudynights.com/forums/topic/693257-firecapture-download-question/)

### Inferences
- The "not well multi-threaded" criticism and the memory-tuning threads point at the same architectural root: a JVM-hosted pipeline where the user must hand-size the heap and the buffer, and where the concurrency model has limits the author has been incrementally addressing (multicore support and threaded histogram in 2.7) rather than redesigning.
- **For the comparison: FireCapture's moat is planetary-specific domain features plus free cross-platform availability, not raw capture engineering.** A native Win32/C++ competitor's natural attack surface is exactly the three things practitioners complain about — memory tuning burden, long-run capture stability, and threading.
- The "dated UI" and "abandonment" angles the brief asked about are **weakly evidenced**: I found no direct quotes calling the UI dated, and no forum discussion of abandonment. The ~2-year release gap is the factual basis for a maintenance concern, not community sentiment I can cite.

### Gaps
- **No sourced quotes criticising the UI as "dated".** The brief's premise could not be substantiated; do not assert it.
- **No sourced community discussion of abandonment concerns.** Raise the maintenance question from the release-date evidence instead.
- Most of the forum material above is **2 to 9 years old**; several threads predate 2.7.14/2.7.15. Current-version sentiment is thinly evidenced.
- No comparative benchmark (FireCapture vs SharpCap frame rates on identical hardware) was found in any source.
