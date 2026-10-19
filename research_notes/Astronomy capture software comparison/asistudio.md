# ZWO ASIStudio — Technical Profile (as of October 2026)

> **Research conditions / caveat for the report writer.** During this research the agent sandbox's HTTPS proxy returned `403 CONNECT` for `www.zwoastro.com`, `us.zwoastro.com`, `astronomy-imaging-camera.com`, `i.cmoscool.com`, `www.cloudynights.com`, `www.sharpcap.co.uk` and `www.firecapture.de`. Direct page fetches of those sites were therefore **not possible**; their content reached me only via the search tool's indexed extracts (quoted snippets with URLs). `sourceforge.net` and `github.com` were fetchable directly and are the strongest primary evidence below. Where a fact rests only on a search-tool extract rather than a page I read end-to-end, it is marked **[extract-only]**.

---

## Q1. What exactly is ASIStudio? Component applications and their roles

### Takeaway
ASIStudio is ZWO's free, bundled desktop suite of single-purpose applications (not one monolithic app) covering planetary capture, deep-sky capture, live stacking, post-capture stacking, FITS viewing, meteor detection and video format conversion. The 2020 canonical list was five apps; by 2023 (v1.8) it had grown to at least eight. "ASIMovie" and "ASIStack" as literal product names could not be confirmed — the real names appear to be **ASIVideoStack** (planetary) and **ASIDeepStack** (deep-sky).

### Cited Findings
- Canonical ZWO description, repeated in the official manual: ASIStudio is "a one-stop astrophotography software that integrates **ASICap** (planetary imaging), **ASIImg** (deep sky imaging), **ASILive** (deep sky imaging live stacking), **ASIFitsView** (fits image viewer), and **ASIDeepStack** (deep sky stacking)" — [ZWO ASIStudio Manual (PDF)](https://i.cmoscool.com/zwo-website/manuals/EN_ASIStudio_Manual.pdf) **[extract-only]**; same five-app framing in the 2020 launch article — [ZWO, "What is ASIStudio and What Can It Bring Us?", 2020-03-26](https://www.zwoastro.com/2020/03/26/what-is-asistudio-and-what-can-it-bring-us/) **[extract-only]**
- **ASICap** — planetary/solar/lunar capture: "ASICap specializes in solar system astrophotography and provides most of the user settable camera functions." — [ZWO user guide, ASIStudio + ASI183MC, 2021-07-13](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **ASIImg** — deep-sky capture: "designed to image deep space objects (DSO) such as nebula clusters, and galaxies… longer exposure times, lower gains." — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **ASILive** — live stacking: "You could see the image stacked in real time with stretch and denoise automatically." — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **ASIFitsView** — FITS viewer, displays 16-bit FITS files. — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **ASIDeepStack** — deep-sky stacker: "process images, darks, flats, and bias frames to gain the best signal to noise ratio (SNR) possible." — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**; third-party summary — [Starfield View, ZWO ASI Studio suite](https://starfieldview.com/imaging-and-processing/zwo-asi-studio-software-suite-for-asi-cameras/)
- **ASIVideoStack** — planetary/solar/lunar *video* stacker (the planetary analogue of ASIDeepStack); present by v1.6.2, with "playback of the original video, stacking modes, and separate post-processing of images." Demonstrated in a v1.6.2 user video. — [TikTok demo of ASIVideoStack 1.6.2](https://www.tiktok.com/@srstuart1/video/7124742963862981934); [Starfield View](https://starfieldview.com/imaging-and-processing/zwo-asi-studio-software-suite-for-asi-cameras/)
- **ASIMeteorCap** — meteor detection and capture, **added in ASIStudio v1.8** (closed beta late 2022, release 2023-01). It "only records the moments when meteors are moving." — [ZWO, "ASIMeteorCap", 2023-02-17](https://www.zwoastro.com/2023/02/17/asimeteorcap-a-powerful-and-easy-to-start-meteor-detection-and-imaging-software/) **[extract-only]**; [ZWO forum: "ASIStudio v1.8 is released!"](https://bbs.zwoastro.com/d/15880-asistudio-v18-is-released) **[extract-only]**; [ZWO forum: v1.8 ASIMeteorCap closed beta](https://bbs.zwoastro.com/d/15463-asistudio-v18-asimeteorcap-is-out-for-a-closed-beta-test) **[extract-only]**
- **ASIFormatTool** — video transcoder, also added in v1.8, launchable from ASIMeteorCap; "shrinks large AVI videos into smaller files while keeping them clear." — [ZWO forum v1.8 release notes](https://bbs.zwoastro.com/d/15880-asistudio-v18-is-released) **[extract-only]**
- v1.8 also raised the **EAF step limit** and made UI changes plus bug fixes. — [ZWO forum v1.8 release notes](https://bbs.zwoastro.com/d/15880-asistudio-v18-is-released) **[extract-only]**

### Inferences
- Classification by domain: **planetary/high-frame-rate** → ASICap, ASIVideoStack; **deep-sky** → ASIImg, ASILive, ASIDeepStack; **utilities** → ASIFitsView, ASIFormatTool; **specialist** → ASIMeteorCap.
- The suite is architecturally a *launcher plus independent executables* rather than one integrated application; the ZWO description ("integrates X, Y, Z") plus the separate-window screenshots in the manual support this, but I did not verify the process model directly.

### Gaps
- **"ASIMovie" and "ASIStack" could not be confirmed as real ZWO product names.** Repeated searches found no ZWO source using either string. The likely intended referents are ASIVideoStack and ASIDeepStack. The report should not assert that ASIMovie/ASIStack exist.
- No component list was obtainable for **V1.19 specifically** — the proxy blocked `zwoastro.com/software_v1/` and the update log. Components added after v1.8 (i.e., in 1.9 → 1.19) are unknown to me.
- No ZWO statement found on whether ASICap and ASIImg share a common capture engine.

---

## Q2. Current version, release date, cadence, active development

### Takeaway
ASIStudio is **actively developed**. Two sources disagree slightly: ZWO's own software page shows **V1.19, dated 2026-03-31**, while the SourceForge mirror's newest folder is **V1.18, dated 2026-03-18**. Cadence since 2021 is roughly 2–6 releases per year, with a dense burst of versions (1.15.2 through 1.18) all appearing on 2026-03-18 — consistent with a backfill/re-upload of the mirror rather than four distinct release events.

### Cited Findings
- ZWO's official software page lists **ASIStudio V1.19, 2026-03-31**, with Windows, macOS and Linux downloads, plus an "Update log" link; the Windows entry also lists an x86 variant, and the macOS section notes a native driver is not required on macOS. — [ZWO Software page](https://www.zwoastro.com/software_v1/) **[extract-only]**
- An older, stale ZWO US store page still showed **V1.12.1, 2024-09-13**. — [us.zwoastro.com/pages/software](https://us.zwoastro.com/pages/software) **[extract-only]**
- SourceForge mirror, fetched directly 2026-10-18 — 36 version folders. Newest **V1.18 (2026-03-18)**. Full dated history: V1.18/1.17/1.16.2/1.16.1/1.16/1.15.2 all 2026-03-18; V1.14 2025-07-11; V1.12 2024-08-20; V1.11 2024-06-03; V1.10.3 2024-04-28; V1.10.2 2024-03-25; V1.10.1 2024-02-19; V1.10, V1.9.5, V1.9.4, V1.9.3 all 2024-01-12; V1.9/1.9.1/1.9.2 2023-11-06; V1.8.2 2023-05-05; V1.8 2023-01-23; V1.7 2022-11-17; V1.6.1 & V1.6 2022-07-07; V1.5.6 & V1.5.5 2022-04-02; V1.5.4 2022-03-16; V1.5.3 & V1.5.2 2021-11-11; V1.5.1 2021-09-26; V1.0/1.1/1.2 2021-08-04; V1.5 2021-07-20; V1.4 & V1.3.2 2021-05-18. — [SourceForge: ASIStudio files](https://sourceforge.net/projects/asistudio/files/)
- The SourceForge "Download Latest Version" button still points at `ASIStudio_V1.8_WinX64_Setup_20230117.zip` (120.7 MB) — the mirror's default link is stale even though newer folders exist. — [SourceForge: ASIStudio files](https://sourceforge.net/projects/asistudio/files/)
- v1.8 release notes exist publicly on ZWO's own forum (dated Jan 2023), showing ZWO does publish changelogs. — [ZWO forum v1.8 release](https://bbs.zwoastro.com/d/15880-asistudio-v18-is-released) **[extract-only]**
- Historical anchor: the suite was announced 2020-03-26. — [ZWO, 2020-03-26](https://www.zwoastro.com/2020/03/26/what-is-asistudio-and-what-can-it-bring-us/) **[extract-only]**

### Inferences
- **Treat V1.19 (2026-03-31) as the current release**, with V1.18 (2026-03-18) as the last version mirrored on SourceForge. The ~2-week gap and the fact that ZWO's own page is the authoritative publisher make V1.19 the better answer.
- The 120.7 MB Windows installer size for v1.8 is large for a native suite of ~8 small apps; that weight is consistent with a bundled GUI framework plus per-app binaries (see Q3).
- Cadence has **slowed in absolute release count but not stopped**: 2021 saw ~8 version folders, 2024 saw ~7, 2025 saw 1 (V1.14, July), and 2026 saw a cluster in March. Development attention is real but lumpy. *(Note: SourceForge folder dates may reflect upload date, not release date, so this cadence reading is soft.)*

### Gaps
- **The V1.19 changelog content is unknown** — `zwoastro.com/software_v1/` and its update log were unreachable. What changed between 1.14 (2025-07) and 1.19 (2026-03) is not documented in anything I could read.
- No statement from ZWO on a support/EOL policy or a public roadmap.

---

## Q3. Implementation language, runtime, UI toolkit, platform matrix

### Takeaway
**No direct evidence of the UI toolkit was obtainable.** Searches for ASIStudio's installer contents, DLL manifest, or any Qt reference returned nothing specific to the product. The circumstantial case for **C++ with Qt** is reasonable but must be labelled as inference, not fact. The platform matrix (Windows incl. x86, macOS, Linux) is sourced.

### Cited Findings
- Platforms: ZWO's software page offers ASIStudio V1.19 for **Windows, macOS and Linux**, with an additional **Windows x86** variant alongside the 64-bit build, and notes macOS needs no separate native driver. — [ZWO Software page](https://www.zwoastro.com/software_v1/) **[extract-only]**
- ZWO's EFW documentation independently confirms the three-platform matrix: "it is recommended to use ASIStudio (supports Windows, macOS, Linux)." — [ZWO EFW-5-2 Product Manual (PDF)](https://i.zwoastro.com/wp-content/uploads/2026/04/40b4bcb0f35ca0ad1dda862569225a5b.pdf) **[extract-only]**
- macOS build existed as early as 2020 (user running it on a MacBook Air). — [British Astronomical Association forum thread](https://britastro.org/?p=581985)
- ASIStudio is distributed as a Windows **zip containing a Setup** (`ASIStudio_V1.8_WinX64_Setup_20230117.zip`, 120.7 MB) — i.e., a conventional native installer, not an MSIX/Store package or a web app. — [SourceForge](https://sourceforge.net/projects/asistudio/files/)
- The underlying ZWO SDK that ASIStudio must sit on is a **C API in native shared libraries** (`ASICamera2`, `EAFFocuser`, `EFWFilter`), with ZWO shipping only `.dll/.so/.dylib` per platform and a C header. — [SharpAstro/ZWOptical.SDK PR #11, merged 2026-09-23](https://github.com/SharpAstro/ZWOptical.SDK/pull/11)

### Inferences (explicitly NOT established fact)
- **Most likely C++ with Qt (moderate confidence, ~70%).** Supporting reasoning: (a) a single visually-identical UI shipped on Windows + macOS + Linux from one Chinese vendor with a small software team is the classic Qt deployment pattern; (b) the native ZWO SDK is a C/C++ library, so a C++ host avoids any binding layer; (c) a ~120 MB Windows installer for a set of modest utilities is consistent with bundled Qt runtime DLLs plus plugins; (d) ZWO ships no .NET runtime dependency and no Electron-style browser payload is implied by the install size. **Counter-considerations:** I found zero sources naming Qt in connection with ASIStudio, and .NET is now also cross-platform — the install size alone does not discriminate. **Do not state Qt as fact in the report.** The cheap decisive test for the user: install ASIStudio on Windows and list the program directory for `Qt5Core.dll`/`Qt6Core.dll` and a `platforms\qwindows.dll` plugin, or run `dumpbin /dependents ASICap.exe`. Generic Qt deployment expectations (Qt5Core/Qt5Gui/Qt5Widgets + `platforms\qwindows.dll` + MSVC or MinGW runtime) are documented at — [Qt Wiki: Deploying Windows Applications](https://wiki.qt.io/Deploying_Windows_Applications)
- **ARM:** no evidence either way for ASIStudio. The Windows x86/x64 pair plus generic "macOS" and "Linux" entries suggests x86-family builds; whether the macOS build is a universal binary (Apple Silicon native) or Intel-only under Rosetta 2 is unresolved. Notably, ZWO *does* ship SDK libraries for `linux-arm64`, `linux-arm` and `osx-arm64` (see Q7), so the toolchain exists — but that says nothing about the ASIStudio GUI builds.

### Gaps
- **No source confirming the UI toolkit, compiler, or language.** This is the single biggest gap in this profile. ZWO publishes no technical/architecture statement about ASIStudio that I could find.
- **No confirmation of Apple Silicon native support** or of a Linux ARM64 / Raspberry Pi ASIStudio build. Searches returned nothing about ASIStudio on ARM.
- No evidence on whether ASIStudio is 32-bit-only, 64-bit-only, or dual on Linux.

---

## Q4. Licensing, cost, source availability, hardware tie

### Takeaway
ASIStudio is **free of charge, closed-source, and functionally tied to ZWO hardware** — ZWO cameras are required for the capture applications, and at least some post-processing paths also reject non-ZWO data.

### Cited Findings
- Described by distributors as "ZWO's free, integrated astrophotography software suite built for ASI cameras." — [Starfield View](https://starfieldview.com/imaging-and-processing/zwo-asi-studio-software-suite-for-asi-cameras/)
- Hardware restriction, user-reported: "ASI Studio was made to be compatible with ASI cameras only." — [Cloudy Nights: ASIStudio not processing/stacking avi taken with a non-ZWO camera](https://www.cloudynights.com/forums/topic/897385-asistudio-not-processingstacking-avi-taken-with-a-non-zwo-camera/) **[extract-only]**
- Accessory restriction, user-reported: the software "would only work with ZWO cameras… you must use a ZWO filter wheel or focuser." — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- ASIStudio is mirrored on SourceForge but **as binaries only** — the project page lists version folders of installers, with no source repository. — [SourceForge: ASIStudio](https://sourceforge.net/projects/asistudio/files/)

### Inferences
- The title above the thread "ASIStudio not processing/stacking avi taken with a **non-ZWO** camera" implies the lock-in extends beyond live device enumeration into *file ingest* for at least one stacking path — i.e., the restriction is a deliberate product decision, not merely an SDK consequence. I did not read the thread body (proxy blocked), so this is read from the title + extract only.
- Free-of-charge + ZWO-hardware-only is a classic razor-and-blades arrangement: the software is a purchase incentive and a retention mechanism for the camera line, not a product in itself. This is an interpretation, not a ZWO statement.

### Gaps
- **No EULA text located.** Whether ASIStudio's licence formally forbids use with non-ZWO hardware, or merely fails to support it technically, is unverified.
- No evidence found of any paid tier, licence key, or online activation.
- No ZWO statement found on whether any ASIStudio component is open source.

---

## Q5. Hardware support surface — how locked to ZWO is it?

### Takeaway
ASIStudio is **ZWO-only by design on the camera side**, with native (driver-free, non-ASCOM) support for ZWO EAF focusers and EFW filter wheels. It is *not* an ASCOM client in the normal sense — ZWO's own documentation positions ASCOM as the route for **other** software to reach ZWO devices, the inverse of what a competing Win32 app's author might expect. No evidence of third-party mount or ASCOM device support inside ASIStudio was found.

### Cited Findings
- **EAF focuser, native:** "The latest version of ASIStudio has native support for the EAF. Other software packages will require the use of an ASCOM driver." — [ZWO, "EAF Software Configuration", 2019-09-18](https://www.zwoastro.com/2019/09/18/software-installation-and-configuration/) **[extract-only]**; also in — [ZWO EAF Manual EN v2.6 (PDF)](https://www.atc-astro.eu/EAF_Manual_EN_V2.6.pdf)
- **ASCOM is the escape hatch, not the architecture:** the EAF manual's procedure is to tick "Use ASCOM Focuser" and choose "ZWO Focuser" in the ASCOM panel — i.e., ASCOM selects a *ZWO* focuser, not an arbitrary one. — [ZWO EAF Manual, ManualsLib p.10](https://www.manualslib.com/manual/1616640/Zwo-Eaf.html?page=10)
- **EAF focus controls in ASICap** are coarse/fine step buttons: "Coarse button when the image looks very fuzzy; fine button when the image looks slightly blurred." — [ZWO EAF Manual EN v2.6](https://www.atc-astro.eu/EAF_Manual_EN_V2.6.pdf)
- **One EAF per instance:** ZWO staff reply — "ASICAP can not control more than 1 EAF unit." (older comment; may be stale) — [ZWO EAF Software Configuration comments](https://www.zwoastro.com/2019/09/18/software-installation-and-configuration/) **[extract-only]**
- **EFW filter wheel:** ZWO lists the supported control stack as "ASIAIR, ASIStudio, ASCOM, and the ZWO SDK"; for a direct computer connection ZWO recommends ASIStudio, adding "you can either use the official software directly without drivers or install the ASCOM driver to use third-party software." — [ZWO EFW-5-2 Manual (PDF)](https://i.zwoastro.com/wp-content/uploads/2026/04/40b4bcb0f35ca0ad1dda862569225a5b.pdf) **[extract-only]**; [ZWO EFW product page](https://us.zwoastro.com/products/efw-filter-wheels) **[extract-only]**
- **Cameras:** ZWO ASI cameras only (see Q4 citations).
- ZWO publishes a separate **ASCOM driver** product for its cameras/focusers/wheels, distinct from ASIStudio. — [ZWO ASCOM Driver page](https://www.zwoastro.com/software/ascom-driver/) **[extract-only]**

### Inferences
- The lock-in is **tight and structural**: a single-vendor device tree (ASI camera + EAF + EFW), no ASCOM/INDI device abstraction layer inside the application, and no third-party camera path. For the user's comparison this is a genuine differentiator — a Win32/C++ app using ASCOM for mounts/focusers can cover devices ASIStudio structurally cannot.
- The "one EAF only" limitation, if still current, is a notable constraint for dual-rig or multi-OTA setups. Age of the source (2019) means it needs re-verification.

### Gaps
- **No evidence found on mount control in ASIStudio at all** — neither ZWO AM-series mount support nor ASCOM/EQMOD telescope support. It is plausible ASIStudio has no mount control whatsoever (that function living in ASIAIR instead), but I found no source to confirm or deny it. The report should flag this as unverified.
- No source on whether ASIStudio can talk to a ZWO **CAA** (camera angle adjuster) or the ZWO **OAG-L/EAF-5V** accessories.
- No count of supported camera models found.

---

## Q6. ASICap in depth, and vs FireCapture / SharpCap

### Takeaway
ASICap is a deliberately simple, "most of the camera's settable functions exposed" capture front end. It writes **AVI for 8-bit and SER for 16-bit**, supports hardware and software binning with ROI, and in several practitioner reports **matches or beats FireCapture on raw frame rate** on the same hardware (notably 170 fps vs 80–145 fps on an ASI290MM). Its deficit against FireCapture is feature depth — no evidence of planet auto-centering/AutoAlign, quality estimation, or the broader FireCapture toolset.

### Cited Findings — capture pipeline and formats
- **Output formats:** ASICap records video as **AVI for 8-bit and SER for 16-bit**. — [ZWO ASIStudio/ASI183MC user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **Binning:** camera hardware binning is limited to Bin1 and Bin2; "the only camera hardware binning available are Bin1 and Bin2 which must be chosen in the Control Section." ASICap exposes Bin1–Bin4, defaulting to **software** pixel merging, with a **hardware Bin checkbox**: "after setting Bin1, Bin2, Bin3 or Bin4, the software algorithm is used for pixel merging by default (software Bin). After checking the hardware Bin, use hardware for pixel merging." — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- ZWO's own characterisation of CMOS hardware binning: "CMOS hardware BIN is more like pixel skip. The frame rate will be faster by pixel extraction, but the signal-to-noise ratio is limited." — [ZWO, "Everything you need to know about Astrophotography Pixel Binning"](https://www.zwoastro.com/2020/06/11/everything-you-need-to-know-about-astrophotography-pixel-binning-the-fundamentals/) **[extract-only]**
- **ROI constraint (anecdotal, one model):** hardware x2 binning worked only when the X dimension was divisible by 16; other hardware bin levels crashed — "The predefined X-ROI should be divisible by 16 for x2 binning." — [SharpCap forum: ASI6200MM-Pro Hardware Binning?](https://forums.sharpcap.co.uk/viewtopic.php?t=6276)
- **USB bandwidth control is exposed:** a user reports "when set bin2, high speed mode with USB speed 100, the frame rate can up to 47fps" in ASICap on an ASI1600MM — i.e., ASICap surfaces the SDK's `USB_BANDWIDTH` and high-speed-mode controls. — [ZWO ASI1600MM product page comments](https://astronomy-imaging-camera.com/product/asi1600mm/) **[extract-only]**
- **Manual focus aid:** coarse/fine EAF step buttons driven from image blur judgement. — [ZWO EAF Manual EN v2.6](https://www.atc-astro.eu/EAF_Manual_EN_V2.6.pdf)
- A dedicated ASICap user guide document exists. — [ASICAP User Guide for Planetary Photography](https://www.scribd.com/document/793446474/ASICAP-helpDoc-EN)

### Cited Findings — achieved frame rates (all practitioner reports, not vendor specs)
- **ASI290MM:** "I did try SharpCap and ASICAP and was able to reach **170 fps** no problem" at full resolution, where the same user's FireCapture topped out at ~80–145 fps. — [Cloudy Nights: Low frame rate with ASI290MM in FireCapture](https://www.cloudynights.com/topic/741977-low-frame-rate-with-asi-290-mm-in-firecapture/) **[extract-only]**
- **ASI174MM:** "I switched to the latest ASIStudio/ASICap and able to capture **185–186 fps (2 ms exposure) with 0 (zero) dropped frames**" writing directly to disk — a SharpCap user who had been dropping ~80% of frames. — [SharpCap forum: 80% of frames dropped when recording](https://forums.sharpcap.co.uk/viewtopic.php?t=6663)
- **ASI462MM:** users saw ~64.8–73.5 fps at 8-bit against a rated 136 fps; USB cable and storage speed both changed the outcome — "Storage speed also plays a role." — [SolarChat: speed issue with ASI462MM](https://solarchatforum.com/viewtopic.php?t=41089)
- **ASI1600MM:** up to 47 fps at bin2 high-speed mode with USB speed 100 in ASICap. — [ZWO ASI1600MM page](https://astronomy-imaging-camera.com/product/asi1600mm/) **[extract-only]**
- Context on the bandwidth ceiling: ZWO states USB 3.0's 5 Gb bandwidth caps the ASI183 Pro at ~19 fps at full resolution, 12-bit normal mode. — [ZWO ASI183 Pro page](https://www.zwoastro.com/product/asi183/) **[extract-only]**
- ASICap also runs on **Android phones** over USB OTG; one user got "5fps or less" on an old phone, similar to ASIAIR Pro on the same setup. — [Cloudy Nights: Lucky imaging — ASIcap on the phone?](https://www.cloudynights.com/topic/816534-lucky-imaging-asicap-on-the-phone/) **[extract-only]**

### Cited Findings — comparison to FireCapture / SharpCap
- **ASICap as the macOS/Linux fallback when FireCapture underperforms:** IceInSpace users reported FireCapture slowing on macOS and switched to ASICap to recover full frame rates; one said ASICap was "almost as good as FC, reaching full frame rates with a modest ROI," another uses it under Linux "because they can't get the same frame rates with FireCapture." — [IceInSpace Solar System forum](https://www.iceinspace.com.au/forum/showthread.php?p=1568059); [same thread, p.1568085](https://www.iceinspace.com.au/forum/showthread.php?p=1568085); [same thread, p.1567320](https://www.iceinspace.com.au/forum/showthread.php?p=1567320)
- **FireCapture's AutoAlign is the differentiator ASICap appears to lack:** "AutoAlign will work on planetary imaging. Before saving each frame the planet will be centered in the frame." — [FireCapture 2.2 feature list](https://rk.edu.pl/en/features-firecapture-22/). **No source was found describing an ASICap equivalent.**
- FireCapture is praised for interface and planetary tracking and is available on macOS "unlike SharpCap"; SharpCap is praised for a "much cleaner look" and simpler learning curve; SharpCap uses SER for planetary video. — [Stargazers Lounge: Any experiences with FireCapture](https://stargazerslounge.com/topic/381607-any-experiences-with-firecapture); [Stargazers Lounge: ASI Studio advice please](https://stargazerslounge.com/topic/363920-asi-studio-advice-please)
- **New-camera support lag is a real differentiator between capture apps:** an ASI676MC was recognised by SharpCap but not FireCapture, and the FireCapture developer's fix was to drop in a newer ZWO SDK DLL. — [AstroBin forum: asi676mc and firecapture](https://app.astrobin.com/forum/topic/133391/asi676mc-and-firecapture)
- General planetary capture software discussion thread. — [Cloudy Nights: planetary capture software and settings](https://www.cloudynights.com/forums/topic/781571-planetary-capture-software-and-settings/)

### Inferences
- **Throughput is not ASICap's weakness.** Multiple independent reports show it at or above FireCapture on identical hardware. The plausible explanation is that ASICap is a thin layer over `ASIGetVideoData` with minimal per-frame processing, whereas FireCapture/SharpCap do more work per frame (overlays, quality metrics, alignment) — but I found no source stating ASICap's internal buffering or thread model, so this is speculation about *why*.
- **ASICap's deficit is feature depth, not speed.** The absence of any source mentioning planet auto-centering, frame quality estimation, or WinJUPOS-compatible mid-time filename stamping in ASICap — in contrast to abundant sources for FireCapture's AutoAlign — is weak evidence of absence, but notable given how searchable those features are.
- For the user's competing app: the achievable-rate bar set by ASICap is roughly **170–186 fps sustained with zero drops at small ROI / short exposure on USB3 mono cameras**, with the real-world constraints being USB cable quality and disk write speed rather than the software.

### Gaps
- **No source found on ASICap's frame buffering architecture, memory buffer sizing, or dropped-frame handling policy.**
- **No source on ASICap timestamping or WinJUPOS compatibility** — whether it writes SER timestamps, UTC frame times, or WinJUPOS-parseable filenames is unverified. This is directly relevant to the comparison and should be flagged as an open question.
- **No source on ASICap histogram, auto-exposure, auto-gain, or quality-estimation features.** The vendor guide extract says it "provides most of the user settable camera functions" but no feature enumeration was obtainable.
- **No source on autoalign / planet centering in ASICap** (absence of evidence only).
- No clean, controlled side-by-side benchmark of ASICap vs FireCapture vs SharpCap exists in any source I found; all comparisons are single-user anecdotes with uncontrolled variables.
- No evidence on ASICap's dark/flat subtraction, debayer options, or ROI-follow behaviour.

---

## Q7. The ZWO SDKs (ASI Camera, EAF, EFW)

### Takeaway
The SDKs are **native C libraries distributed as prebuilt binaries** (no source), versioned independently per device class, and actively maintained — ASI Camera **V1.41 (2026-01-12)**, EAF **V1.8.1 (2026-03-18)**, EFW **V1.8.4 (2025-12-01)**. Platform coverage is broad on the SDK side: win-x64, linux-x64, linux-arm64, linux-arm, osx-x64, osx-arm64. There are real integration hazards documented by third-party binders.

### Cited Findings
- **Current versions and dates:** ASI Camera SDK **V1.41, 2026-01-12**; EAF SDK **V1.8.1, 2026-03-18**; EFW SDK **V1.8.4, 2025-12-01**. — [ZWO Product SDK page](https://www.zwoastro.com/software/product-sdk/) **[extract-only]**
- ZWO's US store page is **stale**, still listing ASI Camera SDK V1.36 (2024-08-16), EAF SDK V1.6 (2023-04-12), EFW SDK V1.7. — [us.zwoastro.com/pages/software](https://us.zwoastro.com/pages/software) **[extract-only]**
- **Library naming is now uniform across platforms:** one name per device — `ASICamera2`, `EAFFocuser`, `EFWFilter`. — [SharpAstro/ZWOptical.SDK PR #11](https://github.com/SharpAstro/ZWOptical.SDK/pull/11)
- **Platform/RID matrix as vendored by a maintained third-party binding:** win-x64, linux-x64, linux-arm64, linux-arm, osx-x64, osx-arm64. **win-x86 and linux-x86 were dropped; armv6 and Android not included; there is no win-arm64 "because ZWO ships no library for it."** — [PR #11](https://github.com/SharpAstro/ZWOptical.SDK/pull/11)
- **Windows EFW DLL depends on the VC++ 2008 runtime (MSVCR90).** The binding maintainer rebuilt `EFWFilter.dll` from ZWO's static library to avoid it, gating on imports limited to KERNEL32, HID, SETUPAPI, USER32, and warning "a future SDK drop may fail to link." — [PR #11](https://github.com/SharpAstro/ZWOptical.SDK/pull/11)
- **Header/enum defects found in the wild:** `EFW_ERROR_CLOSED` is 11 as measured, not 10 as the header declares. `ASI_HUMIDITY` and `ASI_ENABLE_DDR` came from ZWO's bundled C# wrapper, **not** the C header, "where those values now mean other settings" — so code carrying those enum values over from the C# wrapper will set the wrong control on 1.41. — [PR #11](https://github.com/SharpAstro/ZWOptical.SDK/pull/11); [ZWOptical.SDK issue #8](https://github.com/SharpAstro/ZWOptical.SDK/issues/8)
- **Linux quirks:** a libudev workaround is still required; the EAF library ships Bluetooth dependencies (`libWrapperSdbus.so`, `libsdbus-c++.so.2`) that are not needed for direct focuser use. — [PR #11](https://github.com/SharpAstro/ZWOptical.SDK/pull/11)
- **Measured behaviour:** EFW 8-position wheel enumerated, opened, and moved slot 1→3 in **2.4 s**. — [PR #11](https://github.com/SharpAstro/ZWOptical.SDK/pull/11)
- **API surface, documented:** the ASICamera2 SDK manual (Rev 2, 2018-08-08) documents the C API including `ASIGetSDKVersion()` for runtime version query, added in revision 2.7 (2018). — [ZWO ASICamera2 SDK manual (PDF)](https://condorarraytelescope.org/static/pages/pdf/manuals/ZWO-0005.pdf)
- Developer download index. — [ZWO For Developer](https://download.astronomy-imaging-camera.com/for-developer/) **[extract-only]**
- Third-party bindings exist in multiple languages, indicating a stable C ABI: Python — [MPI-IS/camera_zwo_asi](https://github.com/MPI-IS/camera_zwo_asi), [PyPI camera-zwo-asi](https://pypi.org/project/camera-zwo-asi); Go — [mikefsq/goasi asicap](https://pkg.go.dev/github.com/mikefsq/goasi/cmd/asicap); C# — [SharpAstro/ZWOptical.SDK](https://github.com/SharpAstro/ZWOptical.SDK); and an Alpaca bridge tracking SDK bumps — [open-astro/AlpacaBridge issue #1133](https://github.com/open-astro/AlpacaBridge/issues/1133)

### Inferences
- **For the user's Win32/C++ app, the practical facts are:** the camera SDK is at 1.41 (bump from 1.40 was being tracked downstream as of 2026), ZWO has dropped win-x86 from the current SDK drop, and the EFW DLL's MSVCR90 dependency is a live deployment hazard on Windows if the EFW is ever added. The enum drift between ZWO's C# wrapper and the C header is a concrete porting trap.
- **New-camera support is an SDK-version problem, not an application problem** — the ASI676MC/FireCapture case shows a capture app gains support for a new sensor purely by shipping a newer `ASICamera2.dll`. This means ASIStudio's only structural advantage over third-party capture apps on new ZWO hardware is *time to ship the new SDK*, which a third-party developer can close by rebuilding against the current SDK.
- ZWO does not appear to publish SDK source; all evidence points to binary-only distribution with a header. No licence text was located, so the licensing terms remain unverified.

### Gaps
- **No SDK licence text found.** Whether the ASI/EAF/EFW SDKs carry a redistribution licence, a commercial-use restriction, or a hardware-tie clause is unverified. This matters directly for a commercial competing app and should be flagged.
- The current (2026) ASI Camera SDK manual revision was not located — the manual I could read is Rev 2 from 2018. The API has clearly grown since (1.41 vs the 2018 state).
- No public changelog for ASI SDK 1.40→1.41 was located.
- No EAF SDK version-query function confirmed (the binding maintainer's notes do not cover it).

---

## Q8. ASILive and ASIDeepStack — algorithms exposed

### Takeaway
Both are deliberately low-knob tools. ASILive does automatic real-time stacking with automatic stretch and denoise; ASIDeepStack does calibrated stacking with darks/flats/bias. Users report good ease-of-use but a thin algorithm surface, 16-bit (not 32-bit) output, and a recurring star-matching failure mode.

### Cited Findings
- **ASILive:** real-time stacking with "stretch and denoise automatically." — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **ASIDeepStack:** processes lights, darks, flats and bias "to gain the best signal to noise ratio (SNR) possible." — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/) **[extract-only]**
- **Star-registration failure mode:** the "cannot find matched triangles" error recurs. One user: it "periodically prevents live stacking." Another: v1.5.x "produced this error constantly, while 1.8.x never did." A third still hit it on a star-rich field in 1.8.1, "less often than before," stacking roughly half the subs. — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/); [page 4](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/page/4/); [page 5](https://www.cloudynights.com/topic/741000-asistudio-new-version/page-5) **[extract-only]**
- **Bit depth ceiling:** "ASIStudio produces 16-bit FITS files, whereas advanced stackers produce 32-bit results, which limits final image quality." — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **Speed vs alternatives:** one user reported "ASIStudio takes two hours where Siril finishes in 20 minutes." — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **No mosaic support:** "a mosaic stack produced a mess of squares"; a separate thread asks whether neither ASIAIR nor ASIStudio stacks mosaics. — [Cloudy Nights: Neither ASIAIR nor ASIStudio stack mosaics, correct?](https://www.cloudynights.com/forums/topic/897527-neither-asiair-nor-asistudio-stack-mosaics-correct/) **[extract-only]**
- **Positive counterweight:** a 2025-era thread titled "ZWO ASIStudio Deep Stack is ridiculously easy — and good." — [Cloudy Nights](https://www.cloudynights.com/forums/topic/926404-zwo-asistudio-deep-stack-is-ridiculously-easy-and-good/)
- **Planetary-side analogue:** ASIVideoStack stacks planetary/solar/lunar video with selectable stacking modes and separate post-processing. — [ASIVideoStack 1.6.2 demo](https://www.tiktok.com/@srstuart1/video/7124742963862981934)
- Image stacking arrived in ASIStudio **1.6** (2022-07-07). — [Stargazers Lounge: ZWO Asi Studio 1.6 release includes Image Stacking](https://stargazerslounge.com/topic/396405-zwo-asi-studio-16-release-includes-image-stacking-sort-of/); date from [SourceForge](https://sourceforge.net/projects/asistudio/files/)

### Inferences
- The "matched triangles" phrasing indicates a **triangle-similarity star-pattern registration algorithm** (the classic Groth/astrometric triangle-matching approach), rather than phase correlation or feature descriptors. This is inferred from the error string, not from documentation.
- ASIDeepStack/ASILive are positioned against DeepSkyStacker/Siril/ASTAP for beginners: fewer knobs, no 32-bit float pipeline, no mosaic, no drizzle evidence. They are not credible competitors to a serious post-processing chain and are not the product's selling point.
- The algorithm surface is effectively **not exposed** — no documented control over rejection algorithm (sigma clipping, winsorized, etc.), interpolation, normalisation or weighting was found in any source.

### Gaps
- **No documentation found of stacking/rejection algorithms, alignment interpolation method, or any tunable algorithmic parameter** in either ASILive or ASIDeepStack.
- No source on ASIVideoStack's quality-sorting, multipoint alignment, or wavelet/sharpening capability — this is the direct planetary-post analogue to AutoStakkert!/RegiStax and deserves verification.

---

## Q9. Relationship to ASIAIR, and the open-source licensing controversy

### Takeaway
ASIAIR is a separate ARM/Linux embedded controller (Raspberry Pi based, Raspbian) with a tablet/phone client, running an INDI server plus ZWO-proprietary applications. There is a documented, unresolved community dispute over ZWO's compliance with GPL/AGPL/BSD obligations for INDI, PHD2, plate-solving and Stellarium Web Engine components. **No court ruling, FSF finding, or formal enforcement action was found.** Evidence on whether ZWO's attention has "shifted" to ASIAIR is weak either way.

### Cited Findings — architecture
- "The ASIAIR is a Raspberry Pi with a custom power board. It runs Raspbian and contains the licensed software (and scripts) of ZWO." — [GitHub gist: connecting EQ6-R Pro to ASIAIR](https://gist.github.com/timucingelici/217ac492bc2b127c6645ce033463d265)
- INDI community reading of the original specs: "the Raspberry Pi is working using INDI, so this will presumably be indiserver running on this device." — [INDI Forum: ASIAIR = new telescope control device based on Raspberry Pi](https://indilib.org/forum/general/3666-asiair-new-telescope-control-device-based-on-raspberry-pi.html)
- ZWO (quoted, unverified, via the same thread): "ASIAIR only support INDI mount." — [INDI Forum](https://indilib.org/forum/general/3666-asiair-new-telescope-control-device-based-on-raspberry-pi.html)

### Cited Findings — the licensing dispute (all allegations, stated neutrally)
- Bill Nash, an INDI contributor: "As configured, the indiserver distributed by ZWO on the ASIAir is deliberately configured to prohibit the use of hardware from competing vendors." He also contended ZWO's PHD2 use fell short of BSD-3 notice requirements. — [INDI Forum: ASIAIR and opensource software licences](https://indilib.org/forum/development/10380-asiair-and-opensource-software-licences.html)
- Another INDI forum member called ZWO's claim of strict compliance "categorically false," stating "the ASIair gadget is GPL tainted (with the exception of the guider), but there is no public repo for the imager binary; the plate solver source was published only because they were forced to, under threat of license revocation." — [INDI Forum, page 3](https://indilib.org/forum/development/10380-asiair-and-opensource-software-licences/92253.html)
- A claim in the same thread that ZWO used the AGPL-licensed Stellarium Web Engine without the Stellarium developers' awareness. — [INDI Forum](https://indilib.org/forum/development/10380-asiair-and-opensource-software-licences.html)
- **Counter-positions:** Cloudy Nights commenters argue GPL obligations attach to distributed binaries rather than server-side execution, and that ZWO need only release modifications to GPL code, not its proprietary apps. The **PHD2 developers publicly stated ZWO's modified use of PHD2 is permitted under its licence**, while noting the attribution issue. — [Cloudy Nights: AsiAir Support Non ZWO Products](https://www.cloudynights.com/forums/topic/873966-asiair-support-non-zwo-products/); [page 2](https://www.cloudynights.com/topic/873966-asiair-support-non-zwo-products/page-2); [Cloudy Nights: The Open Astro Initiative & ZWO ASIAIR Pro/Plus, p.3](https://www.cloudynights.com/forums/topic/835556-the-open-astro-initiative-zwo-asiair-proplus/page/3/)
- A French-language parallel discussion. — [Webastro: Coup de gueule ZWO — ASIAIR et l'opensource](https://www.webastro.net/forums/topic/194650-coup-de-gueule-zwo-asiair-et-lopensource/page/2/)
- Open alternatives cited by critics: INDIGO Sky on Pi 5, with "a multitude of drivers for cameras, mounts, focusers, etc." — [Stargazers Lounge: ASIAir Alternative — Installing Indigo Sky on Raspberry Pi 5](https://stargazerslounge.com/topic/425538-asiair-alternative-installing-indigo-sky-on-raspberry-pi-5-with-fast-ssd/)

### Inferences
- **State this neutrally in the report:** the factual core that appears uncontested is (a) ASIAIR runs Linux on ARM, (b) it incorporates INDI and PHD2, (c) the shipped INDI configuration restricts third-party camera hardware, and (d) ZWO published plate-solver source at some point. What remains **contested and legally undetermined** is whether the remaining non-published components constitute a licence violation. No neutral adjudication exists in any source found.
- **ASIAIR and ASIStudio are complements, not the same codebase** — different OS, different UI model (tablet app vs desktop), overlapping function. A shared internal capture/stacking core is plausible but unevidenced.
- On "has attention shifted to ASIAIR": the ASIStudio release history (Q2) shows continued but lumpy activity through March 2026, which **does not support a claim of abandonment**. I found no data on ZWO's relative engineering headcount or release velocity between the two products, so the "shift" hypothesis is unproven in both directions. The report should not assert it.

### Gaps
- No ZWO technical documentation or teardown confirming the ASIAIR SoC, OS version, kernel, or software stack in detail. All architecture evidence is secondhand community reporting.
- No 2024–2026 update to the licensing dispute found — no lawsuit, no FSF/SFC action, no formal INDI enforcement.
- No evidence on ASIAIR model lineage/specs (ASIAIR Pro / Plus / Mini / S) as of 2026.
- No evidence on whether ASIStudio and ASIAIR share source.

---

## Q10. Known limitations and practitioner criticism

### Takeaway
The consistent practitioner verdict is: **easy, fast to learn, free, adequate — and shallow.** The specific recurring complaints are ZWO-hardware lock-in, 16-bit output ceiling, star-registration failures in live stacking, poor documentation, no mosaic support, limited colour control, and slow deep-sky stacking relative to Siril. On the planetary side the criticism is about *features*, not throughput.

### Cited Findings
- **Lock-in:** ZWO cameras only; ZWO filter wheel/focuser required. — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**; non-ZWO AVI rejected by the stacker — [Cloudy Nights](https://www.cloudynights.com/forums/topic/897385-asistudio-not-processingstacking-avi-taken-with-a-non-zwo-camera/) **[extract-only]**
- **16-bit output ceiling** vs 32-bit in advanced stackers. — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **"Cannot find matched triangles"** live-stacking failures, improving across versions but not eliminated. — [Cloudy Nights, pages 4–5](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/page/4/)
- **Documentation:** "ZWO documentation is extremely lacking." — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **Colour control:** lack of fine colour-balance tuning; saturation has a floor of 100%. — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **Stacking speed:** 2 hours vs Siril's 20 minutes on a comparable job. — [Cloudy Nights](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **Device enumeration bug:** "creates two devices for the ASI294MM camera." **Auto-histogram bug in 1.7:** "burned out the center of M31." — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**
- **Regression report:** after updating to 1.9.1, deep-sky stacking with dark/flat/bias crashed; light-only stacking was fine. (Single German-language user report.) — surfaced via search of ZWO community discussion of 1.9.1 **[extract-only, original thread URL not captured]**
- **Positive reception of 1.8:** a thread titled "ASI Studio 1.8 Looks Like it Should have been 2.0." — [Cloudy Nights](https://www.cloudynights.com/forums/topic/864505-asi-studio-18-looks-like-it-should-have-been-20/)
- **Positives generally:** "the interface [is] very simple, the process clear, and the software fast on their PC"; "it is easy and does what it has to." — [Cloudy Nights: ASIStudio New Version](https://www.cloudynights.com/forums/topic/741000-asistudio-new-version/) **[extract-only]**; [Cloudy Nights: ASI Studio, p.2](https://www.cloudynights.com/topic/691355-asi-studio/page-2) **[extract-only]**
- General suite discussion threads. — [Cloudy Nights: ZWO AsiStudio](https://www.cloudynights.com/forums/topic/691333-zwo-asistudio/); [Stargazers Lounge: New Software from ZWO, ASI Studio](https://stargazerslounge.com/topic/347146-new-software-from-zwo-asi-studio/)

### Inferences
- **The criticism is overwhelmingly directed at the deep-sky components, not ASICap.** Across all sources surveyed, I found no practitioner complaint about ASICap's capture performance; the complaints are about ASILive/ASIDeepStack quality and about ecosystem lock-in. For a competing *planetary* app, the exploitable gaps against ASICap are therefore feature depth (auto-centering, quality metrics, WinJUPOS-ready timestamping, advanced ROI/autoexposure), hardware breadth (ASCOM/third-party), and documentation — not throughput.
- The **complaint profile has improved over time** (1.5.x "constantly" → 1.8.x "never" for the triangle error), indicating ZWO does fix reported defects, though slowly and with occasional regressions (1.9.1).
- Most criticism dates from 2021–2024 versions. **Nothing in the corpus evaluates V1.15–V1.19 (2026).** Any statement about current quality is therefore extrapolation.

### Gaps
- **No practitioner reviews of ASIStudio V1.15–V1.19 (2026 releases) found at all.** The newest substantive user commentary located is from the 1.8–1.9 era plus one 2025 ASIDeepStack thread.
- No Reddit threads surfaced in the searches despite being requested; the practitioner corpus here is Cloudy Nights, Stargazers Lounge, IceInSpace, SolarChat and the SharpCap forum.
- No stability/crash statistics, no bug tracker — ZWO operates no public issue tracker that I could find.

---

## Cross-cutting: 2020 vs 2026 — what changed

### Takeaway
The 2020 article the user supplied describes a five-application suite at launch. By 2026 the suite is at V1.19 with at least eight components, six years of releases, a macOS and Linux presence, and a substantially revised SDK underneath.

### Cited Findings
| Aspect | 2020 (as per the supplied article) | 2026 (sourced) |
|---|---|---|
| Components | ASICap, ASIImg, ASILive, ASIFitsView, ASIDeepStack ([ZWO 2020-03-26](https://www.zwoastro.com/2020/03/26/what-is-asistudio-and-what-can-it-bring-us/)) | + ASIVideoStack (by v1.6.2, 2022), + ASIMeteorCap and ASIFormatTool (v1.8, 2023-01-23) ([ZWO forum](https://bbs.zwoastro.com/d/15880-asistudio-v18-is-released); [SourceForge](https://sourceforge.net/projects/asistudio/files/)) |
| Version | pre-1.0 era | **V1.19, 2026-03-31** ([ZWO](https://www.zwoastro.com/software_v1/)); SourceForge mirror at V1.18, 2026-03-18 ([SourceForge](https://sourceforge.net/projects/asistudio/files/)) |
| Stacking | not present | added in **V1.6, 2022-07-07** ([SGL](https://stargazerslounge.com/topic/396405-zwo-asi-studio-16-release-includes-image-stacking-sort-of/)) |
| ASI Camera SDK | — | **V1.41, 2026-01-12** ([ZWO Product SDK](https://www.zwoastro.com/software/product-sdk/)) |
| EAF SDK | — | **V1.8.1, 2026-03-18**; EFW SDK **V1.8.4, 2025-12-01** ([ZWO Product SDK](https://www.zwoastro.com/software/product-sdk/)) |

### Gaps
- The 2020 article itself could not be fetched (proxy block); its content is represented here only through search extracts. Treat its specifics as indicative.
