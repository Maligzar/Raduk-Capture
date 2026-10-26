# Comparison With Other Software

How RadukCapture stands against the software planetary imagers actually
run. The ranking first, because it is the part worth knowing: for the
high-frame-rate planetary workload this project exists to serve, the order
is **SharpCap, FireCapture, ASICap, RadukCapture**.

| Rank | Product | Weighted | The one-line reason |
|---|---|---:|---|
| 1 | SharpCap | **8.25** | The only product that is broad, maintained, distributed, and measured in the field at once |
| 2 | FireCapture | **7.50** | Level on planetary depth, ahead on reach, behind on distribution |
| 3 | ASICap | **6.05** | The best raw throughput numbers in the evidence, and little else |
| 4 | **RadukCapture** | **5.15** | Output and planetary depth are competitive; hardware breadth and distribution score 1 |

N.I.N.A. and Sequence Generator Pro are not in that ranking. Neither is a
planetary competitor, and the exclusion is structural rather than a
judgment about quality: see
[N.I.N.A. and SGPro are not in this race](#nina-and-sgpro-are-not-in-this-race).

> **Why this page exists.**
> [Overview](Overview#why-build-this-when-other-astronomy-and-planetary-imaging-applications-already-exist)
> gives the reasons this application was built rather than bought:
> coherence, directness, ownership. This page does not restate them, it
> puts them under evidentiary pressure. The coherence argument survives.
> The competitive position does not.

Researched **18 October 2026**, and not re-checked since: read every
version number, release date, and price below as a statement about that
date rather than about today, and read the
[limitations section](#what-this-comparison-could-not-establish) before
repeating any vendor-sourced fact elsewhere.

---

## N.I.N.A. and SGPro are not in this race

Scoring a product with no video writer on a planetary scale would produce a
number that looks like information and is not. Both exclusions rest on
primary evidence.

**N.I.N.A.** was examined from a full clone of `github.com/isbeorn/nina` at
commit `4742afa`. Its `FileTypeEnum` enumerates exactly **TIFF, FITS, XISF,
and RAW**; writers exist only for FITS and XISF; and a repository-wide grep
for `AviWriter`, `.avi`, and `.ser` across 436,507 lines of C# matched only
a vendored test-images file
([FileTypeEnum.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Core/Enum/FileTypeEnum.cs)).
`ICamera` carries no frame-rate property, no queue depth, and no ring
buffer; live view is `DownloadLiveView(CancellationToken)` polled from a
WPF view model. The maintainer's own view of ROI, its one
planetary-adjacent feature, is that it is "so uncompelling that I would
rather see the concept removed", though that quotation came from a cached
summary of an issue that now returns 404.

**SGPro** says it outright. The vendor lists "Video or planetary cameras"
among the notable exceptions to its equipment support and states the
product "is designed primarily for Deep Space Object capture"
([SGPro FAQ](https://www.sequencegeneratorpro.com/faq_category/sgpro-help/)).
A COM-marshalled `ImageArray` per exposure is structurally incapable of
sustained high-rate streaming: an architectural boundary, not a policy a
checkbox could reverse.

Neither exclusion is a verdict on the product. A deep-sky ranking inverts
this page's order outright: N.I.N.A. first by a wide margin, SGPro second,
SharpCap third, ASIStudio fourth, and FireCapture and RadukCapture not
competing at all.

---

## The criteria and their weights

Stated before the results, because a ranking whose weights arrive after its
scores is an argument working backwards. This ranks for the **planetary,
high-frame-rate use case only**.

| Criterion | Weight | What it measures |
|---|---:|---|
| Sustained lossless throughput and buffering | 25% | Evidence of real sustained capture with drop accounting; buffer design and sizing; behavior at the ceiling |
| Output fidelity and timing correctness | 20% | SER v3 correctness, per-frame timestamps, raw data preservation, honest frame and drop counts |
| In-session planetary decision support | 15% | Centering and tracking, quality metrics, focus aids, ADC, exposure assistance |
| Hardware breadth | 15% | Cameras, focusers, mounts; ASCOM, Alpaca, and INDI reach |
| Distribution, continuity, and support posture | 15% | Installer and releases, release cadence, license, bus factor, documentation |
| Language and runtime decisions | 10% | Determinism, memory-safety posture, rendering path, cross-platform reach, contribution pool |

> **The 10% on language and runtime is itself a finding.** A higher weight
> would be unearned: the failures in the field are not runtime failures. It
> is not zero either, because rendering path, cross-platform reach,
> memory-safety posture, and contribution pool are all traceable to
> language choice. A controlled benchmark showing GC-attributed frame loss
> on identical hardware should raise it. Nothing justifies that yet.

### The scores

| Rank | Product | Throughput (25) | Output (20) | Planetary depth (15) | Hardware (15) | Distribution (15) | Language (10) | **Weighted** |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | **SharpCap** | 8 | 8 | 9 | 9 | 9 | 6 | **8.25** |
| 2 | **FireCapture** | 7 | 8 | 9 | 8 | 6 | 7 | **7.50** |
| 3 | **ASICap** | 9 | 6 | 4 | 3 | 7 | 5 | **6.05** |
| 4 | **RadukCapture** | 6 | 8 | 7 | 1 | 1 | 7 | **5.15** |

**SharpCap** is broad and maintained at once: roughly 14 native camera
families plus ASCOM plus Alpaca, Sensor Analysis and Smart Histogram
computing gain and exposure from measured read noise and sky background,
multi-star FWHM, HFD and Bahtinov focus aids, a free tier, and an active
4.2 beta dated 21 September 2026. Against that: a stable release dating to
22 September 2023, and a dropped-frame support burden large enough to
warrant its own standing guide thread.

**FireCapture** is level on planetary depth and ahead on language. Its
**AutoAlign escalation ladder** (centroid stabilization inside the ROI,
then ROI repositioning, then mount guiding) is the feature most responsible
for its planetary reputation, the frequent confusion being that the base
mode shifts the target within the ROI without moving the mount. It loses on
distribution: the 2.7 line has run since 2020 with no v3, v2.7.15's release
date is contested across three candidates spanning March 2025 to February
2026, nothing dated in 2026 could be verified here, and commercial use is
prohibited. That is release-date evidence alone, which is all there is.

**ASICap** wins outright on raw throughput and loses everything else: ZWO
hardware only, one EAF per instance, no planet centering found, no
documented timestamping, no public issue tracker, and documentation
practitioners describe as extremely lacking. Active, at V1.19 (2026-03-31).

**RadukCapture** scores fourth, and the shape of the score matters more
than the position. The output score of 8 is level with SharpCap and
FireCapture and is earned: correct SER v3 with a 178-byte packed header
under `static_assert`, capture-time timestamps, a per-frame sharpness
sidecar, frame counts patched on close, raw sensor data preserved because
calibration is preview-only by design, and drop accounting in telemetry
([Recording and SER Format](Recording-and-SER-Format)). The hardware and
distribution scores of 1 are arithmetic rather than harsh, itemized under
[Simply behind](#simply-behind), and the throughput score of 6 is generous
to a design unmeasured against real silicon.

---

## Convergent architecture, uneven evidence

Strip away the sequencers and the planetary field is three products plus
this one, converged on one design: a producer/consumer pipeline with a
pre-allocated in-RAM frame pool draining to a disk writer, failing the same
way when the drain cannot keep up. SharpCap's developer says it directly,
that once the buffer fills "the capture rate falls to whatever the SSD can
absorb, and the camera's frames are discarded until a memory slot frees
up". RadukCapture differs only in refusing rather than dropping silently,
and in absorbing roughly 34 seconds of disk stall at 1280x960 RAW8
([Capture Pipeline](Capture-Pipeline)).

The throughput evidence is uneven in a way that matters. **ASICap has the
best field numbers in the corpus**: 185 to 186 fps at 2 ms with zero drops
on an ASI174MM, from a user who had been dropping around 80% of frames in
SharpCap, and 170 fps on an ASI290MM where the same user's FireCapture
topped out between 80 and 145. These are uncontrolled single-user anecdotes
with an acknowledged confound, since a SharpCap moderator diagnosed that
case as a `PreviousFrameNotProcessed` pipeline condition. **No controlled
head-to-head benchmark of any two of these applications exists anywhere in
the gathered evidence.**

**RadukCapture has no field numbers at all.** Its 1280x960 at 120.3 fps and
147.5 MB/s, and its 3840x2160 at 293 MB/s with zero drops, are emulator
results, and the project says so: the real camera over real USB has never
been measured, and overrun behavior under genuine load is untested
([Emulator and Testing](Emulator-and-Testing)). That is the single largest
unknown in this project's competitive position, and it sits on the
heaviest-weighted criterion above.

### Why SER-only

SER is the format the planetary chain consumes, and its lack of structure
is why a capture application writing it is I/O-bound rather than
CPU-bound. FITS is not universal here either, since SharpCap and
FireCapture write it and ASICap does not, but it remains the one output
that would reach a deep-sky or photometric workflow. The phase table in
`RadukCapture-Architecture.md` marks a FITS writer shipped and the tree
contains none; where prose and code disagree this wiki follows the code
([Roadmap and Phase History](Roadmap-and-Phase-History)).

---

## SER timestamps: parity with SharpCap, not an advantage over it

Checked against the source, because the convenient story is wrong and had
started to circulate inside the project.

SharpCap's author has acknowledged that **the SER per-frame timestamp is
the frame-arrival time from the camera SDK with no exposure-midpoint
correction**, while FITS output does subtract exposure time, and that "the
SER code hasn't been updated to the better estimate". RadukCapture does the
same:
[`CaptureThread.cpp#L147`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CaptureThread.cpp#L147)
takes the QPC reading immediately after `GetVideoFrameEx` returns, and
[`SERWriter.cpp#L87`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SERWriter.cpp#L87)
maps it onto a wall clock correlated once at `Open()`. That is
frame-arrival time, not exposure midpoint.

> **This is parity, not a differentiator.** What the Phase 59 fix corrected
> was a worse defect: before it the trailer recorded disk-write time and
> could lag the photons by up to ten seconds with a full backlog, making a
> file useless for WinJUPOS derotation while looking entirely valid.
> Correcting a serious bug brought the product level with the market
> leader. It did not put it ahead, and this wiki should not imply it did.

Subtracting half the exposure would make RadukCapture the only product here
known to stamp exposure midpoints. That is cheap but not a one-liner:
`SERWriter` has no exposure field at all, the value lives in `CaptureThread`
as
[`m_exposureMs`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CaptureThread.h#L75),
so the correction belongs at the stamp site or must be threaded through
`WriteFrame`. FireCapture's behavior is unconfirmed; ASICap's is unknown.

---

## Feature depth across the planetary field

| | SharpCap | FireCapture | ASICap | RadukCapture |
|---|---|---|---|---|
| Video output | SER, AVI | SER, AVI (16-bit removes AVI) | AVI (8-bit), SER (16-bit) | SER v3 only |
| Still output | PNG, TIFF, FITS above a 5 s exposure | FIT, TIF | none found | none |
| Per-frame SER timestamps | Yes, arrival-time | Unconfirmed; WinJUPOS filenames built in | **Open question** | Yes, arrival-time (parity) |
| Planet centering / tracking | Not evidenced as a planetary feature | AutoAlign, three modes; AutoGuide off the main camera | No equivalent found | Measured 2x2 pulse-to-pixel solve, closed loop |
| Focus aids | Multi-star FWHM, HFD, Bahtinov, Pro auto-scans | Third-party autofocus plugin only | Coarse and fine EAF buttons only | 11-point V-curve, 3-point parabola, honest revert |
| ADC support | Not evidenced | Tuning aids (named differentiator) | None found | Edlen advisor over fitted filter band; alignment tool dark |
| Hardware breadth | ~14 native families + ASCOM + Alpaca | ~8 native vendors + ASCOM + INDI | ZWO only (camera, EAF, EFW) | 1 camera, 1 focuser; mount via `IMount`, ASCOM or NexStar |
| Extensibility | IronPython (Pro-gated; no NumPy/SciPy/AstroPy) | Java `.jar` plugins since v2.5 | None | None |
| Platform | Windows, 64-bit only from 2026 | Windows, macOS, Linux x64, aarch64 | Windows, macOS, Linux | Windows only by construction |
| License / cost | Free + GBP 14/yr Pro (2024 price) | Freeware; commercial use prohibited | Free, ZWO hardware only | MPL-2.0 (was PolyForm Noncommercial until 2026-10-26) |

Read the focus-aid row as four different things rather than one shared
capability: only SharpCap ships star-focus measurement.

---

## Language and runtime: what the evidence retires

The analytical heart of the comparison, and it has to survive contact with
evidence unflattering to the native-code position.

### Garbage collection: a negative result, carried honestly

Across every practitioner thread, bug report, and vendor statement
gathered, **not one attributes an astronomy-capture frame drop to garbage
collection.** The documented causes, every time, are USB link bandwidth (a
USB3 camera on a USB 2.0 port or behind a shared hub, cables beyond the
roughly 5 m recommendation), sustained disk write throughput, buffer depth
set too small, ROI size, and CPU saturation during save.

The steelman for managed adequacy is strong. A capture application need not
process each frame within the inter-frame interval, only drain the queue at
the average rate, and a ring of 64 pre-allocated 2 MB buffers absorbs a
320 ms stall at 200 fps and loses nothing, which is why the standard field
fix for drops is "increase the buffer". Pooled and pinned allocation makes
a managed capture loop allocation-free in steady state, and the data path
is native anyway because the vendor SDK DMAs into a native buffer. SharpCap
is the evidence: a .NET application is the de facto standard capture tool
for much of the hobby, with no GC-attributed defect in its forum.

Where the managed position breaks down is **the tail, not the mean**. A
ring buffer converts a pause into a latency spike rather than a loss, right
up until the pause exceeds the ring depth. Every planetary frame buffer is
a .NET Large Object Heap object by a factor of 25 to 50, and the LOH is
uncompacted by default and collected only during Gen2
([LOH](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/large-object-heap)),
so an application clean for the first hour can degrade across a six-hour
night. Microsoft's own low-latency advice is to "minimize the number of new
allocations, in particular allocations onto the large object heap and
pinned objects": exactly what a naive capture loop does.

> **The irreducible point is determinism, not speed.** A C++ ring buffer
> has a provable worst case; a managed one has a worst case that depends on
> the collector, the heap state, the machine, and the runtime version. For
> a hobbyist that is acceptable risk; for occultation timing or lucky
> imaging at the seeing limit it is a real, rarely-exercised limitation.
> That is a values judgment about tolerance for rare catastrophic loss
> dressed as an engineering question, and should be presented as one.

FireCapture's Java position is visible in its own UI: users size the heap
by hand, and a "Force aggressive memory recovery during capture" toggle is
credited with fixing erratic captures. That is the author designing around
the collector rather than the collector solving the problem for free, and
the RAM buffer draws from that heap, so in-RAM capture duration is capped
as a native application's is not. Against that, ZGC reports
sub-millisecond pauses independent of heap size
([JEP 189](https://openjdk.org/jeps/189)).

### Interop cost, retired with arithmetic

A no-op P/Invoke measures **about 46 ns**; an empty JNI call about **22 ns**
on Linux and **65 ns** in a JMH benchmark on Windows. At 200 fps the frame
budget is **5,000,000 ns** and a capture loop makes one to five SDK calls
per frame: **0.001% to 0.005% of the budget**. What matters at the boundary
is whether the frame crosses by pointer or by copy, since a 2 MB per-frame
copy at 200 fps is 400 MB/s of pure memcpy, an implementation choice in any
language rather than a property of one. Thread scheduling is the same
story: `AvSetMmThreadCharacteristics` is an ordinary `avrt.dll` export, so
the MMCSS Pro Audio band is open to managed code too, and the only real
asymmetry is that an application cannot prioritize the GC or JIT. See
[Threading Model](Threading-Model).

### Where language choice genuinely decides

**Rendering.** Microsoft documents that GDI+ alpha blending "is performed
by the CPU, so it does not benefit from hardware acceleration", and a
Microsoft benchmark measured approximately **GDI 120 fps, GDI+ 32 fps, and
Direct2D hardware 415 fps**
([Comparing Direct2D and GDI](https://learn.microsoft.com/en-us/windows/win32/direct2d/comparing-direct2d-and-gdi)).
That figure is Windows 7-era and test-specific, so take it directionally,
but a GDI+ ceiling below 60 fps before any application work is a real
structural fact for a live planetary preview. RadukCapture's D3D11
compute-shader debayer writing a DXGI-shared texture that Direct2D draws
directly is the strongest preview architecture here, and the clearest case
where the API choice bought something measurable
([Rendering and UI](Rendering-and-UI)).

**Cross-platform reach.** Java's payoff is demonstrated rather than
theoretical: FireCapture ships official Windows x64, Linux, and aarch64
builds from a one-person freeware project, down to a Raspberry Pi 5 `.deb`.
N.I.N.A. is the counter-case, since .NET alone does not buy portability if
the UI is WPF, and its Linux/macOS issue is open and labeled `wontfix`.
Pure Win32 plus Direct2D is the most extreme position available, not merely
Windows-only but Win32-only, and porting would mean rewriting all 20,809
lines of `src/gui` plus the GPU and net layers. For planetary that is a
coherent trade, the workflow being "laptop beside the telescope", but it is
permanent and should be described as one.

**Memory safety, where this project has a concrete exposure.** The
convergent empirical figure is about 70%: Chromium's review of 912
high/critical bugs since 2015 found roughly 70% were memory-safety
problems, and Microsoft reported the same across twelve years of security
updates, which is why CISA, NSA, FBI, and four partner agencies urged
manufacturers toward memory-safe languages in December 2023. A hobbyist
application is not that threat model, but the posture applies here:
RadukCapture runs a **hand-rolled RFC 6455 WebSocket server on Winsock2,
with hand-rolled SHA-1 and base64 and a masked client-frame parser,
accepting mount and focuser commands with no authentication**
([`WebSocketServer.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WebSocketServer.cpp)).
Attacker-influenceable length fields, masking, and fragmentation parsed in
C++ on a socket is the exact shape of the bug class that 70% describes, and
it was CRITICAL finding S1 before Phase 57 hardening. Loopback by default
with an explicit `RADUK_WS_LAN=1` opt-in is right
([Telemetry and Dashboard](Telemetry-and-Dashboard)), but a managed runtime
would have removed the memory-safety classes and still not supplied the
authentication.

**The contribution pool.** N.I.N.A.'s per-plugin `AssemblyLoadContext` is a
managed-runtime affordance whose Win32/C++ equivalents are separate
processes with IPC or manual DLL versioning, and it connects directly to
109 plugins and 78 distinct commit-author addresses across 8,152 commits.
RadukCapture has no plugin architecture, no public API, no CI, and a
single-author history. Its license **was** what most limited its
continuity story: PolyForm Noncommercial 1.0.0 is not an open-source
license under the OSD's no-discrimination criterion, therefore
GPL-incompatible, unpackageable by Linux distributions, and unavailable to
contributors whose employers run legal review. That finding is the one
thing in this comparison the project acted on directly: **it relicensed to
MPL-2.0 on 2026-10-26**, matching N.I.N.A., which is the working proof in
this category. What it gave up is the protection PolyForm bought, since a
manufacturer may now ship the capture engine subject to publishing changes
to the covered files.

> **One claim deliberately absent.** No developer-productivity multiplier
> appears on this page: no methodologically defensible study was found, and
> the figures that circulate here are folklore. The defensible version is
> library-level. C#/.NET ships an async model, JSON, HTTP and WebSocket
> stacks, a test culture, and a package manager in the box; a
> pure-Win32/C++20 project must write each one, and this one hand-emits its
> JSON by hand. Where pure Win32 is weakest is **testability**: 13 gated
> tests link 5 of 17 production `.cpp` files, 3,084 test lines against
> 44,754 source.

---

## Where RadukCapture genuinely leads

**The emulator is the clearest technical differentiator in the project.**
[`CameraEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CameraEmulator.h)
is 1,316 lines rendering synthetic RAW8/RAW16 frames with per-planet discs,
surface rotation, moons, turbulence, defocus blur, and per-pixel noise, and
[`MountEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/MountEmulator.h)
implements the real `IMount` and exposes `GetPointingErrorArcsec()`, which
the camera emulator uses to render the planet displaced from center, so the
full `CentroidTracker` loop closes in software. Simulator camera drivers
exist elsewhere, but a coupled physical model where the guide loop closes
against rendered displacement is not standard anywhere in this field. See
[Emulator and Testing](Emulator-and-Testing).

**Guide calibration is measured rather than assumed.**
[`GuideSolve.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/GuideSolve.h)
pulses east, observes where the centroid went, pulses north, repeats, and
derives a 2x2 matrix from pulse milliseconds to pixels that already
contains the camera's rotation in the focuser, the mirror flip, and the
sign of each axis, refusing the solve when a near-zero determinant shows
parallel responses. This is PHD2's approach, strictly better than
orientation-assuming designs. See [Tracking and Guiding](Tracking-and-Guiding).

**The [ADC advisor](Advisors) is more physically careful than anything
found in a mainstream capture application.** It implements Edlen (1966)
refractivity and evaluates dispersion across the band of the filter
actually fitted rather than a fixed 400/700 nm, with site elevation feeding
the pressure term
([`ADCAssistant.h#L106`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ADCAssistant.h#L106)).
FireCapture's ADC tuning aids are the nearest competitor and a different
thing: an alignment aid, not a dispersion prediction.

**The [AI assistant](AI-Assistant) is local-only**, streaming to
`http://localhost:11434/api/chat`. The browser dashboard does call public
internet APIs, so "fully local" covers the application and the assistant,
not the dashboard.

### Simply behind

None of this is arguable, and it is what the hardware and distribution
scores of 1 are made of.

- **No FITS writer, no AVI**, and **no polar alignment**, the last
  backlogged rather than deferred because the 8SE is alt-az with no wedge.
- **No FWHM or Bahtinov focus aid**, which is what star focus needs and
  what the planetary-disc-tuned V-curve cannot substitute for.
- **No plugin architecture, no scripting, no headless mode, no command
  line.** No multi-camera and no guide camera.
- **The camera and the focuser have no interface at all**, one of each
  selected by header name at compile time, so a second brand needs a
  polymorphic abstraction that does not exist ([Device Layer](Device-Layer)).
  The mount is the exception and the pattern the other two should follow:
  `IMount` is real, and
  [`ASCOMMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ASCOMMount.h)
  is 792 working lines of `ITelescopeV3` late binding that
  [`main.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/main.cpp#L720)
  tries **first** on the hardware build.
- **The generic ASCOM path is foreclosed even so**, because
  `FindCPWIDriver()` prefers three Celestron ProgIDs and blacklists
  `ASIMount`, `ASIFocuser`, `DeviceHub`, `JustAHub`, `OmniSim`, `Simulator`,
  and `ScopeSim`. Dropping DeviceHub removes the one driver that would
  multiply breadth, and dropping the simulators leaves the generic path
  untestable indoors. That, not the mount backend, is why breadth scores 1.
- Tuning constants are compiled in: roughly 2000 steps/mm at f/10 in the
  autofocus, an HCG floor of 182 that is an IMX678 property, and a plate
  scale of 0.203"/px belonging to one scope-camera pair.
- **No installer, no tags, no releases, no signed binary**, output paths
  hardcoded to `D:/RadukCapture/`, and a `cmake_minimum_required(VERSION 4.2)`
  floor that will refuse to configure on most current distributions.
- **Windows-only by construction. No CI**, 13 tests touching 5 of 17
  `.cpp` files, and **no authentication on the remote-control path**.
- ~~**PolyForm Noncommercial** forecloses distribution packaging, corporate
  contributors, and GPL compatibility at once, in a project whose bus
  factor is already 1.~~ **Closed 2026-10-26: relicensed to MPL-2.0.** The
  bus factor of 1 is unchanged; what changed is that it is now survivable.

---

## What would change the ranking

In descending order of weighted points returned per unit of work.

1. **Measure the real camera over real USB.** Throughput carries 25% and
   rests on emulator figures the project disclaims. A clear-night FULL RAW8
   run with honest drop accounting moves that score from 6 toward 8 or 9,
   worth **+0.5 to +0.75** with no code written.
2. **Give the camera and focuser interfaces, and open the generic ASCOM
   path.** Hardware breadth carries 15% at a score of 1. The mount is done;
   what is missing is `ICamera` and `IFocuser`, plus a review of the
   `DeviceHub` and simulator exclusions that cap breadth and indoor
   testability. Moving that score to 5 is worth **+0.6**.
3. **Ship an installer and tagged releases**, and remove the hardcoded
   `D:/` paths and the CMake 4.2 floor. Distribution carries 15% at a score
   of 1; getting to 5 is another **+0.6** and needs no new subsystem.
4. **Relicense to MPL-2.0, or dual-license.** This moves no score directly,
   but it is the precondition for the contribution pool, the distro
   packaging, and the GPL compatibility that every other continuity
   improvement depends on.
5. **Subtract half the exposure from the SER trailer timestamp.** Cheap but
   not free, since the exposure value has to reach the stamp site, and it
   would turn parity with SharpCap into a real differentiator for WinJUPOS
   derotation and occultation timing.
6. **Fuzz the WebSocket frame parser and put a token on the command path.**
   The highest-risk component by the standard the memory-safety evidence
   sets. The loopback default is right but is not a substitute.
7. **Write a FITS single-frame writer before an AVI writer.** AVI is a
   legacy container the planetary chain does not want; FITS would reach a
   photometric or deep-sky workflow and stop the phase table claiming a
   capability that does not exist.

Items 1 through 4 together move the weighted score from **5.15 to between
6.95 and 7.1**, past ASICap and within reach of FireCapture, and not one of
them requires inventing anything.

---

## What this changed in the plan

This comparison was not filed and forgotten. `docs/ROADMAP-Next.md` now
carries a **Tier 5, competitive position**, with seven items, P5.1 through
P5.7: exposure-midpoint SER timestamps; `ICamera` and `IFocuser`
interfaces; the generic ASCOM path reconsidered, on the corrected premise
that the mount backend already works and is preferred at startup while
`DeviceHub` and the simulators are what is excluded; distribution split out
of the very-hard phase 20, which is where it had been parked and is why it
never moved; the CMake 4.2 floor; WebSocket fuzzing and authentication; and
the PolyForm Noncommercial versus MPL-2.0 license decision, **taken on
2026-10-26 in favour of MPL-2.0**. P5.6 sits in the live queue on its
safety dimension, and P5.7 was held out of the queue as an owner decision
rather than an engineering task. The tier
is not reproduced here: read it in `docs/ROADMAP-Next.md`, and see
[Roadmap and Phase History](Roadmap-and-Phase-History) for how the tiers
are organized and what the phase record already holds.

The comparison also re-weighted items already on the plan. Work scoring
against hardware breadth or distribution is worth more than its effort
estimate suggested, and work deepening the planetary feature set is worth
less, because depth is where this project already scores well.

> **The highest-value unticked item in the project is not in the roadmap at
> all.** By this page's weighting it is item 3 of
> `docs/CLEAR-NIGHT-CHECKLIST.md`, checking the plate scale against the sky,
> and it now leads the consolidated live-sky debt. Every arcsecond figure
> the application reports, the ADC advice, the drift readout, and every
> overlay scale derives from `PlateScaleArcsecPx = 0.203`, calculated from
> focal length and pixel pitch and never once compared against a solved
> field. The app already logs that comparison on every successful plate
> solve. The fix costs no code and one clear night.

---

## What this comparison could not establish

Read this before repeating any vendor-sourced fact from this page.

**The research environment's network policy denied direct access to
firecapture.de, sharpcap.co.uk (including its docs and forums),
zwoastro.com, sequencegeneratorpro.com, nighttime-imaging.eu,
cloudynights.com, and archive.org.** Almost every vendor-sourced claim
above therefore arrived through a search index that read those pages
server-side, so quoted strings are second-hand transcriptions and prices,
build numbers, and release dates should be re-verified against the live
pages. The GBP 14/year SharpCap Pro figure is the 2024 price.

Two exceptions carry the strongest evidence. **The N.I.N.A. figures come
from a full clone** at commit `4742afa`, so the counts, target frameworks,
commit history, and absent SER/AVI writer are directly measured and
reproducible, and **the ZWO SDK findings** came from fetched GitHub
sources. RadukCapture's own figures were measured against the working tree
on 18 October 2026.

Five things could not be settled. They are open questions rather than
absences, because inferring them would be inventing them:

1. **ASIStudio's UI toolkit.** No source names one.
2. **SGPro's UI framework.** Its prerequisite list naming both .NET
   Framework 3.0-3.5 and 4.7.2 fits WPF and WinForms equally.
3. **Whether SharpCap 4.x has moved off .NET Framework 4.8.** The downloads
   extract says 4.1 "now runs on Microsoft .NET 8 (previously .NET 7)" and
   ships self-contained, while the last developer-sourced requirement in
   hand is .NET Framework 4.8 for the 3.3 line. Both are second-hand and
   they are not reconciled.
4. **FireCapture's UI toolkit.** Java is certain from four independent
   angles; the toolkit is not evidenced.
5. **ASICap's timestamping and buffering behavior**, entirely undocumented,
   and bearing directly on the ranking above.

Two claims that circulate about FireCapture appear nowhere in this page's
reasoning, being supported by nothing gathered: that its UI is dated, and
that the community harbors abandonment concerns.

---

## Documentation drift, named

This comparison measured the tree, and the measurement disagreed with the
project's own prose in six places: [Home](Home) claimed "~34,500 lines
across 86 source files" against an actual **44,754 lines across 100 files**
(83 `.h` totaling 30,194 and 17 `.cpp` totaling 14,560), since corrected
there; "there is no automated test suite" against 13 gated tests, also
since corrected; the
emulator documented at 984 lines against 1,316; a bright-star table header
claiming 255 stars against 97; the FITS writer marked shipped when none
exists; and **`ASCOMMount` labeled Superseded when it is the mount path
tried first**.

> **The last is the most consequential, because it propagated.**
> "Superseded", and the claim that all known CPWI drivers are blacklisted,
> are the wiki's own words at [Device Layer](Device-Layer#backend-status)
> and in the [Code Map](Code-Map) table, not the header's, and the research
> behind this page inherited the error from the wiki rather than from the
> source. The source says the opposite:
> [`src/main.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/main.cpp#L720)
> calls `ASCOMMount::FindCPWIDriver()` before anything else on the hardware
> build and falls back to NexStar direct, and CPWI is the first preferred
> ProgID rather than a blacklisted one. Those pages are left unamended
> because correcting the label alone would contradict the prose around it.

Every prose number and every status label in this project needs checking
against the tree before it is repeated.

---

**Next:** [Overview](Overview) · [Capture Pipeline](Capture-Pipeline) · [Roadmap and Phase History](Roadmap-and-Phase-History)
