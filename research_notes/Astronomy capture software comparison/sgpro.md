# Sequence Generator Pro (SGPro) — Technical, Commercial and Maintenance Profile

> **Research conditions / caveat on sourcing.** Compiled 2026-10-18. Direct HTTP fetch of `www.sequencegeneratorpro.com`, `forum.sequencegeneratorpro.com`, `help.sequencegeneratorpro.com` and `www.cloudynights.com` was **not possible from this environment** (DNS resolution failure in the fetch tool; HTTP CONNECT 403 from the egress proxy). All findings below come from a search index that retrieved and quoted the content of those pages. Quoted strings are therefore second-hand renderings of primary pages rather than pages I read byte-for-byte. Where that materially weakens a claim I say so. **Anything requiring byte-exact verification (current checkout price, current build number) should be re-checked against the live site before publication.**

---

## Q1. Implementation language, runtime and UI framework

### Takeaway
SGPro is, with **high confidence, a Microsoft .NET Framework (not .NET Core/5+) Windows desktop application** — the installer prerequisite list and runtime crash logs both name .NET Framework versions. The specific UI toolkit (WinForms vs WPF) is **not directly confirmed by any source I could reach**; treat "WinForms" as an inference, not a fact.

### Cited Findings
- Prerequisites listed for build **4.5.0.1652** are **ASCOM 6, .NET Framework 3.0–3.5, and .NET Framework 4.7.2** — [Main Sequence Software forum, 4.5.0.1652 release post](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- A forum report for SGPro 3.1 states "the new SGPro seems to require at least .NET 4.7.2"; the installer aborted on a Windows 10 Enterprise LTSB machine that had only .NET 4.6 — [forum: Primalucelab Eagle 2 and SGPro 3.1 .NET FRAMEWORK blocked](https://forum.sequencegeneratorpro.com/t/primalucelab-eagle-2-and-sgpro-3-1-net-framework-blocked-solved/11309)
- Forum diagnostic logs from a user of **SGPro 4.2.0.895 (64-bit)** show the app running on "**.NET Framework Version: 4.8**" — [forum: FRESH installation of SGPro (4.2.0.895 (64-bit)) does not even start](https://forum.sequencegeneratorpro.com/t/fresh-installation-of-sgpro-4-2-0-895-64-bit-does-not-even-start/17274)
- SGPro ships in both **32-bit and 64-bit** builds, distributed as separate downloads — [Download: SGPro 4 (32-bit)](https://www.sequencegeneratorpro.com/download/sequence-generator-pro-432-bit-63/); [Download: SGPro 4 (64-bit) BETA](https://www.sequencegeneratorpro.com/download/sequence-generator-pro-464-bit-beta-97/)
- SGPro "won't even start without ASCOM installed"; reinstalling ASCOM fixed it for that user — [forum: Trouble Running SGP](https://forum.sequencegeneratorpro.com/t/trouble-running-sgp/14657)
- OS floor: "SGPro will run on almost any Windows 10 or 11 machine (either 32 or 64 bit)"; recommended **1 GHz+ CPU, 6 GB+ RAM**, ~**1 GB** disk for SGPro and its cache, **1920×1080 or higher** display — [forum: Recommended System Specifications for SGPro](https://forum.sequencegeneratorpro.com/t/recommended-system-specifications-for-sgpro/16966)
- Contrast datapoint for the competitor: **N.I.N.A. 3.0 (18 Mar 2024) moved N.I.N.A. to the .NET 8 framework** — [nighttime-imaging.eu, N.I.N.A. 3.0](https://nighttime-imaging.eu/2024/03/18/n-i-n-a-3-0/)

### Inferences
- **Language is C#** — inferred. .NET Framework 4.7.2 + a closed-source Windows astro app of this era makes C# overwhelmingly likely, but I found **no developer statement naming the language**. Do not print "written in C#" as sourced fact.
- **UI framework is WinForms** — inferred, moderate confidence only. The .NET Framework 3.0–3.5 prerequisite alongside 4.7.2 is suggestive of mixed-era assemblies, and 3.0 is the WPF/WCF-introducing release, so the 3.0–3.5 entry could equally indicate a **WPF** dependency or a third-party control library targeting 3.5. I could not find an installer manifest, dependency list, or developer post that settles WinForms vs WPF. **Flag this as unverified in the comparison.**
- SGPro remains on **.NET Framework 4.x**, a Windows-only, feature-frozen runtime (Microsoft ships no new .NET Framework versions beyond 4.8/4.8.1), whereas N.I.N.A. migrated to the actively developed **.NET 8** in March 2024. This is a real, citable architectural divergence: SGPro's runtime receives security fixes only.
- The hard ASCOM-6 startup dependency implies the COM interop layer is not an optional plug-in but is wired into application startup.

### Gaps
- No access to the installer's file manifest, so no list of bundled DLLs (which would settle WinForms vs WPF immediately via `System.Windows.Forms.dll` vs `PresentationFramework.dll`).
- No developer statement found naming the implementation language or UI toolkit.
- The official system-requirements page on `www.sequencegeneratorpro.com` / `help.sequencegeneratorpro.com` could not be reached; all runtime-requirement data above is from forum posts and release posts, not the canonical requirements page.
- Could not establish whether the 64-bit build is still flagged BETA as of October 2026 (the download-page listing retrieved still carried "(BETA)" in its title).

---

## Q2. Current version, release date, cadence, and whether development is active

### Takeaway
As of 2026-10-18 the newest release I can verify is **SGPro 4.5.0.1652, posted 3 August 2026**. Development is **active but slow and low-amplitude**: six maintenance builds across the 4.5 line in 2026, all small bug-fix/diagnostic changes, and **no release or verifiable developer communication found for September or October 2026** — a ~2.5-month gap at time of writing.

### Cited Findings
- **SGPro 4.5 released 22 March 2026.** Headline changes: overhauled help system, new Control Panel layout, new Equipment Profile Manager layout, in-app settings search, periodic and reactive centering, improved telescope slaving — [forum: SGPro v4.5 is Released!](https://forum.sequencegeneratorpro.com/t/sgpro-v4-5-is-released/23794); goal of the 4.5 line described as overhauling "settings, options and configuration" — [forum: SGPro 4.5 Beta Enhanced Release Notes](https://forum.sequencegeneratorpro.com/t/sgpro-4-5-beta-enhanced-release-notes/18985)
- A **4.5 Release Candidate** preceded it — [forum: SGPro 4.5 Release Candidate is here](https://forum.sequencegeneratorpro.com/t/sgpro-4-5-release-candidate-is-here/23779)
- **2026 general-release build train (4.5.0.x), oldest → newest:**
  - **1631** — adds a Control Panel tab consolidating shutdown options; Planning Assistant and ASTAP fixes — [forum 23804](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1631-is-released-for-general-use/23804)
  - **1636 — 19 April 2026** — changes PlateSolve3 interaction "for better resilience with Windows security"; fixes equipment profiles failing to load — [forum 23831](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1636-is-released-for-general-use/23831)
  - **1638 — 24 April 2026** — new ASTAP-native setting forcing ASTAP to use SGPro's scale hint instead of auto-determining image scale — [forum 23840](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1638-is-released-for-general-use/23840)
  - **1642** — flat-frame exposure accepts up to 4 decimal places; ASTAP no longer defaults to auto scale hint; **"A fix for a memory leak caused by large sequences that will ultimately crash SGPro"** — [forum 23865](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1642-is-released-for-general-use/23865)
  - **1649 — 13 June 2026** — better diagnostics for ASTAP solve failures; changed how SGPro loads the PlateSolve3 solver; fixed auto-rotation to target angles to use the sky value rather than the mechanical one — [forum 23885](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1649-is-released-for-general-use/23885)
  - **1652 — 3 August 2026 (latest verified)** — sequences can run a user script on sequence start; Mosaic and Framing Wizard now remembers last used angles — [forum 23933](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- **The official download page lagged the forum**: a retrieval of the downloads listing still showed build **4.5.0.1578 dated January 2026** while the forum had announced 1652 — [Downloads](https://www.sequencegeneratorpro.com/downloads/); [Download: SGPro 4 (32-bit)](https://www.sequencegeneratorpro.com/download/sequence-generator-pro-432-bit-63/)
- **Roadmap, ~February 2026** (post retrieved with an age of ~253 days, i.e. roughly 2026-02-07): developer **Ken Sturrock** describes **FocusTune**, a planned automated focusing wizard where "SGPro [asks] the user some questions and then [automates] the hard parts"; states that Auto Focus "sampling data collection has room for improvement"; says there are "lots of user requests inbound in 4.6" and he "will update with a list of them in the near future" — [forum: SGPro 4.5 and 4.6 Quick Updates](https://forum.sequencegeneratorpro.com/t/sgpro-4-5-and-4-6-quick-updates/23757)
- **4.6 is referenced as a real future version** in a breaking-change notice: users of the legacy ASTAP mode must switch to "ASTAP (native)" **before SGPro 4.6** or the Chain Solver will skip that solver — [forum: Notice to ASTAP Plate Solver users](https://forum.sequencegeneratorpro.com/t/notice-to-astap-plate-solver-users/23821)
- The official **PDF documentation available for download is dated January 2021** — [SGPro Documentation (PDF)](https://www.sequencegeneratorpro.com/download/sgpro-documentation-pdf/)
- Prior major versions for cadence context: **4.3** — [forum 18321](https://forum.sequencegeneratorpro.com/t/sgpro-4-3-is-released/18321); **4.4** (build 4.4.0.1339) — [forum 18651](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-4-0-1339-is-released-for-general-use/18651)

### Inferences
- **Not abandoned, but close to maintenance mode.** 4.5 shipped a settings/UI reorganisation rather than new imaging capability; the six subsequent builds are overwhelmingly plate-solver plumbing (ASTAP/PlateSolve3) and bug fixes. The only genuinely new capability in the whole 2026 train is "run a user script on sequence start" (1652). That is the signature of a small team keeping the lights on, not of feature competition.
- **Single-maintainer risk.** Only one developer name (Ken Sturrock) appears in roadmap and release communication in the material I retrieved. Bus-factor appears to be 1–2.
- **A memory leak that "will ultimately crash SGPro" on large sequences persisted into 2026** (fixed in 1642). For an unattended all-night imaging application this is a notable quality signal and worth quoting directly in a comparison.
- **Last verifiable developer communication: 3 August 2026** (the 1652 release post). I could find nothing dated September or October 2026. I cannot distinguish "no releases in that window" from "search index has not crawled them" — state it as "no release found after 3 Aug 2026" rather than "no release occurred."

### Gaps
- Could not confirm whether any build after 4.5.0.1652 exists (the Releases forum section could not be browsed directly).
- No published 4.6 feature list or ship date; the promised "list of user requests" for 4.6 was not found.
- No changelog page covering the whole 4.5 line in one place was reachable, so the build list above may be incomplete.
- Could not verify current activity level on the forum (posts/day, developer response latency) because the forum was unreachable.

---

## Q3. Commercial model, price, licensing, ownership

### Takeaway
SGPro is sold by **Main Sequence Software** as a **first-year purchase plus optional annual renewal**: **US$149.00 up front, then US$59.00/year**, covering **3 simultaneous machine activations** by default (4/5/6 purchasable). It is **not a hard subscription** — a lapsed subscriber keeps and can keep using the last version released while their subscription was active.

### Cited Findings
- Listed price: "**$149.00 now then $59.00 / year**" — [SGPro product category page](https://www.sequencegeneratorpro.com/product-category/sgpro/); [Sequence Generator Pro subscription product page](https://www.sequencegeneratorpro.com/product/sequence-generator-pro-subscription/)
- Lapse behaviour, per the official product page: a subscription "is NOT required in order to continue using it"; the subscription "covers future updates and premium support"; if it lapses "you can still install and run the most recent version released during your active subscription period" and "even if you decide [to] let your subscription lapse, you will always be able to use some version of SGPro" — [Sequence Generator Pro subscription product page](https://www.sequencegeneratorpro.com/product/sequence-generator-pro-subscription/)
- Activations: "The default option allows for **3 simultaneous installs**" and "you may also choose to purchase a total of **4, 5 or 6 activations**"; activations can be reclaimed from machines no longer in use — [Sequence Generator Pro subscription product page](https://www.sequencegeneratorpro.com/product/sequence-generator-pro-subscription/)
- **Transition terms when the model changed** (announcement): the **$149 initial cost was waived for SGPro 3 owners**, and **SGPro 2 owners received a $100 USD credit**; renewals **$59/year plus sales tax where applicable** (one forum post cites ~**$63.87** for a Texas buyer) — [forum: Important Announcement for SGPro!](https://forum.sequencegeneratorpro.com/t/important-announcement-for-sgpro/13301)
- The business-model change was contentious enough to generate dedicated threads — [forum: Subscription based model](https://forum.sequencegeneratorpro.com/t/subscription-based-model/13508); [Cloudy Nights: Sequence Generator Pro Business Model Change?](https://www.cloudynights.com/topic/766663-sequence-generator-pro-business-model-change/); [Stargazers Lounge: Sequence Generator Pro - still value for money?](https://stargazerslounge.com/topic/362872-sequence-generator-pro-still-value-for-money/)
- A long-time user cited the subscription model specifically as a reason to leave SGP — [Cloudy Nights: is it time to just ditch SGP for Nina?](https://www.cloudynights.com/forums/topic/1003351-is-it-time-to-just-ditch-sgp-for-nina)
- **Third-party "lifetime licence" listings exist at $29.99–$49.99 and should be treated as unverified/likely unauthorised** — [Narhy Store](https://narhy.com/product/sequence-generator-pro-lifetime-license/). Do not cite these as SGPro pricing.
- Competitor baseline: **N.I.N.A. is free** and open-source — [nighttime-imaging.eu download](https://nighttime-imaging.eu/download/)

### Inferences
- The licence is best described as **"perpetual-use with a paid update/support subscription"**, not SaaS. The software does not stop working; only entitlement to new builds and premium support stops. This is a meaningful distinction for a comparison and is often misreported in forum threads as a pure subscription.
- **3 activations as the default is generous relative to typical enterprise desktop licensing** and is a genuine point in SGPro's favour for multi-rig/observatory users.
- Ownership: the vendor is **Main Sequence Software** (the product site and the forum both carry that name). I did not find a corporate registration, company size, or legal-entity detail.

### Gaps
- **The $149 / $59 figures could not be read off a live checkout page from this environment** — they come from a search-index rendering of the product pages. Given the explicit instruction that current price is one of the two most important facts, **re-verify directly at `https://www.sequencegeneratorpro.com/product/sequence-generator-pro-subscription/` before publishing.** I also cannot date the last price change.
- **No trial terms found.** I could not locate any statement of a free-trial length (SGPro historically offered a 45-day trial; I could not verify that in this session, so it is omitted).
- No information found on refund policy, educational pricing, or currency options other than USD.
- No information found on the legal owner/entity behind Main Sequence Software, number of employees, or whether it is a sole proprietorship.

---

## Q4. Positioning: what SGPro is for, and whether it does planetary / video (SER, AVI)

### Takeaway
SGPro is **exclusively a deep-sky-object (DSO) imaging sequencer and observatory-automation controller**. The vendor's own help pages state it is **not compatible with planetary or video cameras**. There is **no SER or AVI support of any kind**. For a planetary/high-frame-rate capture application, SGPro is not a competitor — it is an adjacent, non-overlapping product.

### Cited Findings
- Official vendor help text: SGPro "is not currently compatible with planetary or video cameras" and "is designed primarily for **Deep Space Object capture**", listing "Video or planetary cameras" among its "notable exceptions" to equipment support — [SGPro Technical Help / FAQ](https://www.sequencegeneratorpro.com/faq_category/sgpro-help/)
- Supported camera classes per the vendor: **CCD, CMOS, Canon DSLRs and Nikon DSLRs**, plus "almost any equipment that uses the **ASCOM** standard" — [SGPro Technical Help / FAQ](https://www.sequencegeneratorpro.com/faq_category/sgpro-help/); [SGPro product page](https://www.sequencegeneratorpro.com/sgpro/)
- A third-party download listing claims support for "subframes and high speed cameras" — [Softpedia: Sequence Generator Pro](https://www.softpedia.com/get/Multimedia/Graphic/Graphic-Others/Sequence-Generator-Pro.shtml). This is a redistributor's blurb, contradicts the vendor FAQ on intent, and should not be read as video capture.

### Inferences
- **No SER/AVI container support** — inferred from the vendor's explicit "not compatible with planetary or video cameras" plus the complete absence of any SER/AVI mention across every SGPro source retrieved. Stated as a negative finding, not as a quoted vendor denial of those specific formats.
- The output model is **single-frame FITS per exposure** driven by a sequencer, with the entire architecture (ASCOM `Camera.StartExposure`/`ImageArray` polling, per-frame plate-solve/dither/autofocus hooks) built around multi-second to multi-minute exposures. A COM-marshalled `ImageArray` per frame is structurally incapable of sustained high-frame-rate streaming — this is an architectural, not merely a policy, exclusion.
- For the competitive comparison: **SGPro and a Win32/C++ planetary capture app do not overlap in function.** The honest framing is "different problem domains sharing a hardware abstraction layer (ASCOM)", and the only real competitive surface is the shared ASCOM/driver stack and the user's Windows desktop.

### Gaps
- Could not confirm the exact FITS/XISF output format options or whether any raw-stream export exists, as the file-format documentation page was unreachable.

---

## Q5. Hardware abstraction: ASCOM, Alpaca, native SDKs, device classes

### Takeaway
SGPro is **ASCOM/COM-centric to the point of a hard startup dependency**. There is **no evidence of native Alpaca (HTTP/REST) client support**; Alpaca devices are reached only through the ASCOM Platform's COM-to-Alpaca dynamic-driver shim. Native vendor SDKs are essentially absent — the one known native driver (ZWO ASI) is reported unmaintained.

### Cited Findings
- SGPro will not start at all without ASCOM installed — [forum: Trouble Running SGP](https://forum.sequencegeneratorpro.com/t/trouble-running-sgp/14657); **ASCOM 6** is a listed prerequisite — [forum 23933](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- Vendor claim: works with "almost any equipment that uses the ASCOM standard" — [SGPro product page](https://www.sequencegeneratorpro.com/sgpro/)
- **Alpaca is indirect, not native.** ASCOM's Bob Denny, quoted on Cloudy Nights: neither N.I.N.A. nor SGP use the ASCOM Chooser, "which is how you create the link from COM to Alpaca"; once a dynamic driver is created with the Chooser it is "just another ASCOM (COM) driver" and "you will find it in SGP as well." The same source frames **direct Alpaca support as a future possibility** developers "might want to add… once more Alpaca devices become available." — [Cloudy Nights: Looking for example ALPACA code…, p.2](https://www.cloudynights.com/forums/topic/816784-looking-for-example-alpaca-code-to-jump-start-a-project/page/2/). **Note: that thread was ~1,230 days old at retrieval (i.e. ~2022), so it predates SGPro 4.4/4.5 and may be stale.**
- **Native ZWO ASI driver is legacy/unmaintained**: "at one time SGP had a native driver when the ZWO driver was unreliable. It is now out of date and I do not think it is being updated." — [forum: Native driver for ASI camera](https://forum.sequencegeneratorpro.com/t/native-driver-for-asi-camera/15331)
- **Alpaca used as a workaround for an SGPro/COM limitation**: a user with two identical cameras resolved a driver conflict via a remote server and Alpaca; Bob Denny notes "Alpaca has no such limitation on hosting two identical devices" — [Cloudy Nights ALPACA thread](https://www.cloudynights.com/forums/topic/816784-looking-for-example-alpaca-code-to-jump-start-a-project/page/2/)
- Generic ASCOM driver-disconnection problems are a recurring forum topic — [forum: ASCOM device drivers disconnecting](https://forum.sequencegeneratorpro.com/t/ascom-device-drivers-disconnecting/17944)
- **Device classes confirmed in use:** camera (incl. DSLR), **telescope/mount** (auto meridian flip, slaving), **focuser** (autofocus, filter offsets), **filter wheel**, **rotator** (rotation angle is part of the centering action), **guider** (external apps) — [Cloudy Nights SGPro review](https://www.cloudynights.com/articles/astro-gear-today/reviews/software/automating-astrophotography-sequence-generator-pro-review-r4601/); [forum: How to configure ASTAP](https://forum.sequencegeneratorpro.com/t/how-to-configure-astap/13135)
- SGPro ships an **"SGPro API Guider"** — an ASCOM **camera driver** that exposes an onboard guide camera for autoguiding, i.e. SGPro also *publishes* an ASCOM interface, not only consumes one — [Download: SGPro API Guider](https://www.sequencegeneratorpro.com/download/sgpro-api-guider/)
- Contrast: **N.I.N.A. 3.2 (12 Nov 2025) added native ZWO EAF and SVBony camera drivers** — [N.I.N.A. Version 3.2 is here!](https://nighttime-imaging.eu/2025/11/12/n-i-n-a-version-3-2-is-here/)

### Inferences
- The architecture is a **COM/in-process ASCOM client**. The practical consequences are the classic ones a Win32/C++ author will recognise: apartment-threading constraints, driver crashes taking the host down, no two-instances-of-identical-device support, and no remote/cross-platform device access without a shim.
- **SGPro's native-SDK story is weaker than N.I.N.A.'s and appears to be regressing** (an abandoned ASI native driver vs. N.I.N.A. actively adding native drivers in late 2025). This matters for download-speed and sensor-feature exposure.
- **Flat panel / flat box, safety monitor, weather station, switch:** SGPro is widely understood to support these ASCOM device classes (safety monitor and flat box in particular are standard parts of its unattended-observatory story, and its shutdown/recovery features imply them). **However I found no citable source for any of these four classes in this session.** Treat as unverified.

### Gaps
- **No citation found** for: ASCOM SafetyMonitor, ObservingConditions/weather, CoverCalibrator/flat box, or Switch support. The vendor equipment-support page was unreachable.
- Could not determine whether SGPro 4.4/4.5 added any direct Alpaca client; the only Alpaca evidence is ~2022-vintage. **This is the single most likely-stale finding in this document.**
- No information on whether SGPro supports ASCOM Platform 7.

---

## Q6. Feature coverage

### Takeaway
SGPro covers the standard unattended-DSO-session feature set — targets/events sequencer, auto meridian flip, autofocus with filter offsets, plate-solve centering, framing & mosaic wizard, external guider integration with dithering, equipment profiles, and recovery. Its 2026 differentiators are thin; recent work has concentrated on plate-solver plumbing.

### Cited Findings
- **Auto meridian flip**: the routine "will plate-solve the current location to verify the coordinates, pause the autoguider, perform the meridian flip slew, re-center the telescope using plate-solving, re-start the autoguider, and continue imaging" — [Cloudy Nights SGPro review](https://www.cloudynights.com/articles/astro-gear-today/reviews/software/automating-astrophotography-sequence-generator-pro-review-r4601/). Settings live on the equipment profile's Telescope tab: "Use Auto Meridian Flip", "Mins past Meridian Flip", "Wait for Meridian", "Auto Center After Meridian Flip" — [forum: Option box to recalibrate PHD2 after Meridian flip](https://forum.sequencegeneratorpro.com/t/option-box-to-recalibrate-phd2-after-meridian-flip/9997)
- **Guiding integration**: "You can connect SGPro to the autoguiding software of your choice (**PHD2, Astroart, Metaguide, Direct Mount Guider if using TheSkyX, or SkyGuard/SkyGuide**) so that SGPro can initiate, pause, and stop guiding as needed during the sequence"; a "**Pause guiding during auto focus**" option exists and is reported to prevent PHD2 losing its star during an AF run with an OAG — [Cloudy Nights SGPro review](https://www.cloudynights.com/articles/astro-gear-today/reviews/software/automating-astrophotography-sequence-generator-pro-review-r4601/)
- **Plate solving**: SGPro supports **PlateSolve2**, **PlateSolve3**, and **ASTAP**. ASTAP got "formal support" in **4.4** as "**ASTAP (native)**", alongside a legacy ASTAP mode "that copied the PlateSolve2 communication contract"; the legacy mode will be dropped and the **Chain Solver** will skip it from **4.6** — [forum: Notice to ASTAP Plate Solver users](https://forum.sequencegeneratorpro.com/t/notice-to-astap-plate-solver-users/23821). PlateSolve3 loading was changed in **1636** and again in **1649** — [forum 23831](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1636-is-released-for-general-use/23831); [forum 23885](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1649-is-released-for-general-use/23885)
- SGPro has a **"Chain Solver"** concept (ordered fallback across configured solvers) — [forum: Notice to ASTAP Plate Solver users](https://forum.sequencegeneratorpro.com/t/notice-to-astap-plate-solver-users/23821)
- Solver behaviour notes: PlateSolve2 "is very critical on the image scale"; ASTAP in SGPro "is not a blind solver — you need the RA, DEC and angle hints" (2020 post, may be stale); users report ASTAP solving quickly even with ~1° pointing error — [forum: Anyone Using ASTAP for Plate Solving in SGP?](https://forum.sequencegeneratorpro.com/t/anyone-using-astap-for-plate-solving-in-sgp/11803); [forum: How to configure ASTAP](https://forum.sequencegeneratorpro.com/t/how-to-configure-astap/13135)
- **Centering + rotator**: "If you have a rotator, the rotation angle will also be part of the centering action"; users report plate solving "could take a minute or more (especially when using a rotater)" before switching to ASTAP — [forum: How to configure ASTAP](https://forum.sequencegeneratorpro.com/t/how-to-configure-astap/13135)
- **4.5 added "periodic and reactive centering"** and improved telescope slaving — [forum: SGPro v4.5 is Released!](https://forum.sequencegeneratorpro.com/t/sgpro-v4-5-is-released/23794)
- **Mosaic & Framing Wizard** is an add-on module; it reads its parameters from the equipment profile (pixel scale, camera pixel size) and handles both mosaics and framing of a sub-region of a larger target — [Mosaic and Framing Wizard](https://www.sequencegeneratorpro.com/mosaic-and-framing-wizard/); [Sara Wager: creating a mosaic in software](https://www.swagastro.com/sgpro---creating-a-mosaic-in-software.html); [theargents: Framing Wizard, Sequences & Mosaic Planning](https://theargents.wordpress.com/2022/08/07/framing-wizard-sequences-mosaic-planning-part-1/). Build **1652** made it remember last used angles — [forum 23933](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- **Equipment profiles**: "you can save an equipment profile with all the details for each component… if you run multiple rigs, you can easily switch between equipment profiles, and apply them to specific sequence files" — [Cloudy Nights SGPro review](https://www.cloudynights.com/articles/astro-gear-today/reviews/software/automating-astrophotography-sequence-generator-pro-review-r4601/). A new **Equipment Profile Manager layout** shipped in 4.5 — [forum: SGPro v4.5 is Released!](https://forum.sequencegeneratorpro.com/t/sgpro-v4-5-is-released/23794)
- **Recovery mode** is a per-profile setting: one user reports "recovery mode kicked in and when the clouds cleared, images were in focus and continued as if nothing had happened"; an observatory log records a sequence aborting on guide-star loss because the profile "lacked the 'Recovery' setting" — [Cloudy Nights SGPro review](https://www.cloudynights.com/articles/astro-gear-today/reviews/software/automating-astrophotography-sequence-generator-pro-review-r4601/); [IMT3b Observatory, SGPro tag](https://imt.gingergeeks.co.uk/tag/sgpro/)
- **End-of-sequence / shutdown**: build **1631** added "a Control Panel tab that gathers all shutdown options in one place" — [forum 23804](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1631-is-released-for-general-use/23804)
- **Scripting hooks**: build **1652** added the ability to "run user scripts when a sequence starts" — [forum 23933](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- **Planning Assistant** exists as a named subsystem (fixes in build 1631) — [forum 23804](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1631-is-released-for-general-use/23804)
- **Autofocus is a known weak point the developer acknowledges**: Ken Sturrock states AF "sampling data collection has room for improvement" and plans **FocusTune**, an automated focusing-setup wizard — [forum: SGPro 4.5 and 4.6 Quick Updates](https://forum.sequencegeneratorpro.com/t/sgpro-4-5-and-4-6-quick-updates/23757)
- A third-party overview characterises **advanced scripting** and **broad equipment compatibility** as SGPro strengths — [SaaSHub: SGPro vs N.I.N.A.](https://www.saashub.com/compare-sequence-generator-pro-vs-n-i-n-a)

### Inferences
- The sequencer model is **target-centric with per-target event lists** (filter / exposure / count / binning rows), configured against a saved equipment profile — inferred from the profile/sequence-file relationship described in the Cloudy Nights review and the mosaic-wizard workflow, not from a quoted spec.
- **Dithering** is performed via the external guider (PHD2 et al.) under SGPro's control, consistent with SGPro initiating/pausing/stopping guiding. I found **no explicit citation for dither settings**; treat as inferred.
- Plate-solving work dominated the 2026 changelog, which suggests the solver integration was the main source of field failures.

### Gaps
- **Autofocus algorithm not identified.** No source describes the curve-fitting method (hyperbolic, parabolic, V-curve, HFR vs FWHM metric) or step logic. One forum user notes ASTAP "provides a good metric for use in autofocussing" but nothing confirms what SGPro itself uses. **Do not assert an algorithm.**
- **Filter offsets**: not directly cited in this session, though the feature is well established in SGPro. Unverified here.
- **Astrometry.net and PinPoint integration**: not confirmed in this session. SGPro has historically supported both (Astrometry.net as a blind-solve fallback, PinPoint as a licensed local solver), but **I found no 2025/2026 source**, so their current status — especially whether they survive the 4.6 solver cleanup — is unknown.
- **Sky Atlas**: no source found describing SGPro's built-in object catalogue/atlas in this session.
- **Notifications** (email/Pushover/etc.): no source found.
- **Dithering**: no explicit source found.
- Exact autofocus trigger conditions (temperature delta, time, filter change, HFR drift) not sourced.

---

## Q7. Extensibility: plugin, API, scripting surface

### Takeaway
SGPro has a **documented HTTP API** served by the running application on **localhost:59590** (since v2.3), plus **external process/script hooks**. It has **no plugin architecture** — this is the sharpest structural contrast with N.I.N.A., which has a first-class plugin system.

### Cited Findings
- Official API documentation: "SGPro uses an **HTTP based API**"; it is "for external applications that capture image data or control equipment connected to SGPro"; calls can be made using "**SOAP, JSON, XML, JSV or CSV**"; the docs are reached by starting SGPro (**version 2.3 or higher**) and opening the local metadata page on **port 59590**; each call's documentation lists description, supported HTTP verbs, and input parameters — [SGPro Help: API Documentation](https://help.sequencegeneratorpro.com/API%20Documentation/index.html)
- **Script hooks**: sequences can run a user script on sequence start (added build **1652**, Aug 2026) — [forum 23933](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- SGPro **exposes an ASCOM camera driver** ("SGPro API Guider") so external guiding software can use an onboard guide camera through SGPro — [Download: SGPro API Guider](https://www.sequencegeneratorpro.com/download/sgpro-api-guider/)
- A third-party comparison lists "advanced scripting" among SGPro's strengths — [SaaSHub: SGPro vs N.I.N.A.](https://www.saashub.com/compare-sequence-generator-pro-vs-n-i-n-a)
- Competitor contrast: **N.I.N.A. has a plugin system**, and 3.2 isolated "plugins… in their own AssemblyLoadContexts" to reduce inter-plugin dependency clashes — [N.I.N.A. Version 3.2 is here!](https://nighttime-imaging.eu/2025/11/12/n-i-n-a-version-3-2-is-here/); users cite NINA's plugin system as an advantage — [Cloudy Nights: is it time to just ditch SGP for Nina?](https://www.cloudynights.com/forums/topic/1003351-is-it-time-to-just-ditch-sgp-for-nina)

### Inferences
- The SOAP/JSON/XML/JSV/CSV combination and self-hosted metadata page is the signature of **ServiceStack** hosted in-process — a .NET-stack detail consistent with the .NET Framework finding. **Inference, not sourced.**
- The API appears to be **self-documenting at runtime only** (you must run SGPro to read it), which means there is **no publicly browsable endpoint reference** — a real friction point for integrators and a notable contrast with a public OpenAPI spec.
- Extensibility is **out-of-process only** (HTTP clients, launched scripts, an ASCOM driver boundary). There is no in-process extension point, so third parties cannot add UI, sequencer instructions, or device logic the way N.I.N.A. plugin authors can.
- The whole 2026 extensibility delta is one new script trigger point. Extensibility is not an area of active investment.

### Gaps
- Could not enumerate the API's actual endpoints, auth model (if any), or whether it binds only to loopback, because the runtime metadata page requires a running SGPro instance.
- No published API versioning or stability policy found.
- Unknown what scripting languages/host the "user scripts" feature invokes (likely arbitrary executables/batch, but unconfirmed).

---

## Q8. Known limitations and practitioner criticism

### Takeaway
The recurring, independently repeated criticisms are: **slow inter-frame downtime**, **unreliable/slow autofocus**, **stability and session-abort problems**, **a steep configuration learning curve**, and **the subscription**. Several of these are corroborated by SGPro's own 2026 changelog (a crash-causing memory leak; developer-acknowledged autofocus weakness).

### Cited Findings
- **Inter-frame downtime** is the most-cited defect. Users describe SGP's "download and downtime between subs" as too slow, contrasted with N.I.N.A.'s "multi-threaded approach" that "keeps the next exposure starting almost immediately" and "handles star detection in parallel while the next sub is already being captured". One user with a RASA8 and a small-well-depth camera forced to 30-second subs said "SGP's downtime between subs made it unusable" — [Cloudy Nights: is it time to just ditch SGP for Nina?](https://www.cloudynights.com/forums/topic/1003351-is-it-time-to-just-ditch-sgp-for-nina); [Cloudy Nights: NINA vs SGP](https://www.cloudynights.com/forums/topic/832652-nina-vs-sgp/)
- **Autofocus**: described as "failing when the focuser moves far enough during the initial step, and being slow"; N.I.N.A.'s routine praised for handling parabolic fitting well — [Cloudy Nights: First time trying out N.I.N.A. instead of SGP](https://www.cloudynights.com/forums/topic/653180-first-time-trying-out-nina-instead-of-sgp/page/45/). **Corroborated by the developer**, who says AF "sampling data collection has room for improvement" — [forum: SGPro 4.5 and 4.6 Quick Updates](https://forum.sequencegeneratorpro.com/t/sgpro-4-5-and-4-6-quick-updates/23757)
- **Stability / spurious session aborts**: "after a long road of instability and unpredictability, [I] decided to switch to NINA"; SGPro "seemed to find reasons to end the imaging session" — [Cloudy Nights: Switching from SGP to NINA](https://www.cloudynights.com/forums/topic/1005170-switching-from-sgp-to-nina). **Corroborated by SGPro's own release notes**: build 1642 fixed "a memory leak caused by large sequences that will ultimately crash SGPro" — [forum 23865](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1642-is-released-for-general-use/23865). A third-party comparison also cites "occasional stability problems after updates" — [SaaSHub](https://www.saashub.com/compare-sequence-generator-pro-vs-n-i-n-a)
- **Complexity**: "SGPro's setup isn't intuitive and the 200-plus page manual is off-putting" — [Cloudy Nights: First time trying out N.I.N.A. instead of SGP](https://www.cloudynights.com/forums/topic/653180-first-time-trying-out-nina-instead-of-sgp/page/10/); another user found SGP "frustrating to sync across components" — [Cloudy Nights: is it time to just ditch SGP for Nina?](https://www.cloudynights.com/forums/topic/1003351-is-it-time-to-just-ditch-sgp-for-nina)
- **Plate-solve slowness pre-ASTAP**: solves "could take a minute or more (especially when using a rotater)" before switching to ASTAP — [forum: How to configure ASTAP](https://forum.sequencegeneratorpro.com/t/how-to-configure-astap/13135)
- **Driver fragility**: recurring ASCOM disconnection reports — [forum: ASCOM device drivers disconnecting](https://forum.sequencegeneratorpro.com/t/ascom-device-drivers-disconnecting/17944); inability to host two identical devices via COM, requiring an Alpaca workaround — [Cloudy Nights ALPACA thread](https://www.cloudynights.com/forums/topic/816784-looking-for-example-alpaca-code-to-jump-start-a-project/page/2/)
- **Windows-only** is called out as a limitation — [SaaSHub](https://www.saashub.com/compare-sequence-generator-pro-vs-n-i-n-a)
- **Where SGPro still wins, per its defenders**: a more flexible sequencer for scheduled start/end times — one user "needed a sequence to start and end at set times because of a limited sky window; SGP could do that, and NINA could only use a delay from when the sequence started"; a better histogram; broad equipment compatibility — [Cloudy Nights: NINA vs SGP](https://www.cloudynights.com/forums/topic/832652-nina-vs-sgp/); [SaaSHub](https://www.saashub.com/compare-sequence-generator-pro-vs-n-i-n-a)
- **Contested criticism**: on recovery, a user argues "NINA actually has far more recovery options than SGP, it just does it very differently", using triggers like "center after drift" — i.e. SGPro's recovery is *different*, not categorically weaker — [Cloudy Nights: NINA vs SGP](https://www.cloudynights.com/forums/topic/832652-nina-vs-sgp/)

### Inferences
- The downtime complaint is **architecturally consistent** with a COM/ASCOM single-pipeline design that serialises exposure → `ImageArray` marshal → save → star-detect → dither → next exposure, versus a pipelined/multi-threaded design. The repeated independent reports plus the architectural plausibility make this the strongest technical criticism to carry into a comparison.
- Several of the most-cited complaints are **old** (the heavily-referenced "First time trying out N.I.N.A. instead of SGP" thread runs to 45+ pages and spans years). Some may be partly addressed in 4.5. **Date-qualify them.**

### Gaps
- **No Reddit r/AskAstrophotography threads were retrievable in this session** — searches returned Cloudy Nights and comparison sites instead. The requested Reddit evidence is missing.
- No quantitative benchmark of SGPro vs N.I.N.A. inter-frame overhead (seconds/frame) found. The claim remains qualitative.
- Exact posting dates for individual Cloudy Nights quotes could not be captured (the site was unreachable for direct fetch); only relative recency and thread IDs are available. Thread IDs 1003351 and 1005170 are high-numbered and therefore recent (2024–2025 era), but **I cannot date them precisely**.

---

## Q9. Health signal: SGPro vs N.I.N.A. cadence and community activity

### Takeaway
On the evidence available, **SGPro is best characterised as actively maintained but in competitive decline, trending toward maintenance mode** — small bug-fix builds, a runtime generation behind, no plugin ecosystem, a paid model against a free competitor, and a community narrative that has already tipped. It is **not abandoned**: there was a major 4.5 release in March 2026, six subsequent builds, and a stated 4.6 plan.

### Cited Findings
- **SGPro 2026 cadence**: 4.5 (22 Mar) plus six maintenance builds — 1631, 1636 (19 Apr), 1638 (24 Apr), 1642, 1649 (13 Jun), 1652 (3 Aug). Content is overwhelmingly plate-solver fixes and diagnostics; the only new capability is "run user scripts on sequence start" — [forum 23794](https://forum.sequencegeneratorpro.com/t/sgpro-v4-5-is-released/23794); [23804](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1631-is-released-for-general-use/23804); [23831](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1636-is-released-for-general-use/23831); [23840](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1638-is-released-for-general-use/23840); [23865](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1642-is-released-for-general-use/23865); [23885](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1649-is-released-for-general-use/23885); [23933](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- **No SGPro release found dated September or October 2026** (gap of ~2.5 months from 1652 to the 2026-10-18 research date) — negative result across [multiple release-post searches](https://forum.sequencegeneratorpro.com/t/sequence-generator-pro-4-5-0-1652-is-released-for-general-use/23933)
- **Official download page lagged the forum** by ~7 months at one retrieval (1578/January 2026 on the download page vs 1652 announced on the forum) — [Downloads](https://www.sequencegeneratorpro.com/downloads/)
- **Official PDF documentation dated January 2021** — [SGPro Documentation (PDF)](https://www.sequencegeneratorpro.com/download/sgpro-documentation-pdf/)
- **N.I.N.A. cadence**: 3.0 on **18 Mar 2024** (migration to **.NET 8**) — [N.I.N.A. 3.0](https://nighttime-imaging.eu/2024/03/18/n-i-n-a-3-0/); 3.2 on **12 Nov 2025**, adding **native ZWO EAF and SVBony drivers** and per-plugin `AssemblyLoadContext` isolation — [N.I.N.A. 3.2](https://nighttime-imaging.eu/2025/11/12/n-i-n-a-version-3-2-is-here/)
- **Community narrative**: "the community has largely moved to NINA because its development has outpaced SGP, citing its open source nature" — [Cloudy Nights: NINA vs SGP](https://www.cloudynights.com/forums/topic/832652-nina-vs-sgp/); "I've used SGP and I've used NINA. NINA is better." — [Cloudy Nights: is it time to just ditch SGP for Nina?](https://www.cloudynights.com/forums/topic/1003351-is-it-time-to-just-ditch-sgp-for-nina)
- **Volume and longevity of the migration discussion** is itself the signal: a dedicated Cloudy Nights thread "First time trying out N.I.N.A. instead of SGP" runs to **at least 45 pages** — [p.45](https://www.cloudynights.com/forums/topic/653180-first-time-trying-out-nina-instead-of-sgp/page/45/) — alongside separate threads [NINA vs SGP (832652)](https://www.cloudynights.com/forums/topic/832652-nina-vs-sgp/), [Another NINA vs SGP topic (716445)](https://www.cloudynights.com/topic/716445-another-nina-vs-sgp-topic/), [Should I now switch to NINA? (750688)](https://www.cloudynights.com/topic/750688-should-i-now-switch-to-nina/page-2), [is it time to just ditch SGP for Nina? (1003351)](https://www.cloudynights.com/forums/topic/1003351-is-it-time-to-just-ditch-sgp-for-nina) and [Switching from SGP to NINA (1005170)](https://www.cloudynights.com/forums/topic/1005170-switching-from-sgp-to-nina). **Note there is no symmetric "switching from NINA to SGP" thread corpus in any of these results.**
- The **subscription change** (SGPro 3 → 4) is explicitly named by users as a departure trigger and generated dedicated backlash threads — [Cloudy Nights: Sequence Generator Pro Business Model Change?](https://www.cloudynights.com/topic/766663-sequence-generator-pro-business-model-change/); [forum: Subscription based model](https://forum.sequencegeneratorpro.com/t/subscription-based-model/13508); [Stargazers Lounge: still value for money?](https://stargazerslounge.com/topic/362872-sequence-generator-pro-still-value-for-money/)

### Inferences
- **When the migration started**: the N.I.N.A.-vs-SGP thread corpus begins with Cloudy Nights topic **653180** ("First time trying out N.I.N.A. instead of SGP"), whose low topic ID places it well before 716445, 750688, 832652 and the 2024–25-era 1003351/1005170. The migration conversation therefore **predates the subscription change** (thread 766663) and was **accelerated, not caused, by it**. Topic IDs are an ordering proxy only — **I could not read actual post dates** because Cloudy Nights was unreachable for direct fetch.
- **Direction of travel is one-way.** The complete absence of a reverse-migration thread corpus across these searches is, in itself, a stronger signal than any individual quote.
- **Runtime generation gap**: SGPro on .NET Framework 4.7.2/4.8 (feature-frozen, security-fixes-only, Windows-only) vs N.I.N.A. on .NET 8 since March 2024. Over a 5-year horizon this is the most consequential architectural difference between the two.
- **Honest scorecard for an engineering comparison**: SGPro is *maintained* (a 2026 major release and six builds is more than most hobby-scale commercial astro software), but it is *not competing on features*. The 2026 changelog contains no new imaging capability; N.I.N.A.'s 2025 release added native drivers and a plugin-isolation redesign. Calling SGPro "abandoned" would be wrong and easily refuted; calling it "in maintenance mode, losing share to a free, faster-moving, plugin-extensible competitor on a modern runtime" is defensible from the cited evidence.
- **For the specific comparison at hand** (a Win32/C++ planetary capture app): SGPro is not a competitor at all. It explicitly excludes planetary and video capture. The only legitimate comparison axes are hardware-abstraction strategy (ASCOM-COM vs native SDK), extensibility model (out-of-process HTTP API vs whatever the C++ app offers), and commercial model.

### Gaps
- **No quantitative community-activity metrics** (forum posts/month, active users, download counts, market-share survey) for either product. The "widespread migration" claim rests on thread volume and asymmetry, which is suggestive but not measured. **Do not state a percentage or user count.**
- Could not verify whether a N.I.N.A. release exists after 3.2 (12 Nov 2025) — nothing from 2026 surfaced, so N.I.N.A.'s own 2026 cadence is **unestablished**, which weakens any direct 2026-vs-2026 cadence comparison. Verify at [nighttime-imaging.eu/download](https://nighttime-imaging.eu/download/).
- No data on SGPro's revenue, licence sales, or user base.
- Could not measure SGPro forum developer-response latency in 2026.

---

## Summary table (confidence-labelled)

| Attribute | Value | Confidence / basis |
|---|---|---|
| Vendor | Main Sequence Software | Sourced (product site + forum branding) |
| Platform | Windows 10/11, 32- and 64-bit | Sourced (forum) |
| Runtime | .NET Framework 4.7.2 required; observed running on 4.8; also lists .NET Framework 3.0–3.5 | Sourced (4.5.0.1652 release post; user logs) |
| Language | C# | **Inferred** — no source names it |
| UI framework | WinForms | **Inferred, weak** — 3.0–3.5 prerequisite is equally consistent with WPF |
| Latest version | 4.5.0.1652 | Sourced; **dated 3 Aug 2026**; nothing newer found as of 2026-10-18 |
| 4.5 GA date | 22 March 2026 | Sourced |
| 2026 build count | 7 (4.5 + six 4.5.0.x) | Sourced |
| Next version | 4.6 planned (FocusTune, AF rework, user requests) | Sourced, ~Feb 2026; **no date** |
| Price | **US$149.00 first, then US$59.00/year** | Sourced via search index, **not read from live checkout** |
| Licence model | Perpetual use of last version received + paid update/support subscription | Sourced |
| Activations | 3 default; 4/5/6 purchasable; reclaimable | Sourced |
| Trial | **Unknown** | Gap |
| Domain | Deep-sky sequencer only | Sourced |
| Planetary / SER / AVI | **Not supported** — vendor FAQ excludes planetary and video cameras | Sourced (negative) |
| Hardware abstraction | ASCOM/COM, hard startup dependency on ASCOM 6 | Sourced |
| Alpaca | Indirect only (COM dynamic-driver shim); no native client found | Sourced but **evidence ~2022, possibly stale** |
| Native SDKs | Legacy ZWO ASI driver, reported unmaintained | Sourced |
| Plate solvers | PlateSolve2, PlateSolve3, ASTAP (native, since 4.4); Chain Solver fallback | Sourced |
| Astrometry.net / PinPoint | **Unverified in this session** | Gap |
| Guiders | PHD2, Astroart, MetaGuide, TheSkyX Direct Guider, SkyGuard/SkyGuide | Sourced |
| Autofocus algorithm | **Unknown**; developer acknowledges it needs work | Gap + sourced admission |
| Plugins | **None** | Sourced (absence) + contrast with N.I.N.A. |
| API | HTTP, self-documenting at `localhost:59590`, SOAP/JSON/XML/JSV/CSV, since v2.3 | Sourced |
| Script hooks | User script on sequence start (added Aug 2026) | Sourced |
| Health verdict | Maintained; maintenance-mode trajectory; losing share to N.I.N.A. | **Inferred from sourced cadence + community evidence** |
