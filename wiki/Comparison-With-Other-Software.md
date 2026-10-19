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
> [Overview](Overview#why-build-it-rather-than-use-sharpcap-or-firecapture)
> states the reasons this application was built rather than bought, and
> those reasons are about coherence, directness, and ownership. They are
> not claims about being better than SharpCap, and this page does not
> restate them. What it does, for the first time anywhere in the project,
> is put that argument under evidentiary pressure: it asks what the field
> actually does, measures RadukCapture against it on stated criteria, and
> reports the result where the result is unflattering. The coherence
> argument survives. The competitive position does not.

Researched **18 October 2026**. Version numbers, release dates, and prices
go stale fast in this category, so read every such figure as a statement
about what was true when this page was written. The
[limitations section](#what-this-comparison-could-not-establish) is not
boilerplate and should be read before any vendor-sourced fact below is
repeated anywhere else.

---

## N.I.N.A. and SGPro are not in this race

Scoring a product with no video writer on a planetary scale would produce a
number that looks like information and is not. Both exclusions rest on
primary evidence.

**N.I.N.A.** was examined from a full clone of `github.com/isbeorn/nina` at
commit `4742afa`, so these are measured facts. Its `FileTypeEnum`
enumerates exactly **TIFF, FITS, XISF, and RAW**; writer implementations
exist only for FITS and XISF; and a repository-wide grep for `AviWriter`,
`.avi`, and `.ser` across 436,507 lines of C# matched only a vendored
test-images file
([FileTypeEnum.cs](https://github.com/isbeorn/nina/blob/develop/NINA.Core/Enum/FileTypeEnum.cs)).
`ICamera` carries no frame-rate property, no queue depth, and no ring
buffer; live view is `DownloadLiveView(CancellationToken)` polled in a loop
from a WPF view model. The maintainer's own position on the one
planetary-adjacent feature in the product is that ROI "in general is used
to increase the frame rate of the camera for planetary imaging, which
Firecapture is most geared towards", and that N.I.N.A.'s implementation is
"so uncompelling that I would rather see the concept removed". That
quotation reached this research through a cached summary of a tracker issue
that now returns 404, so treat it as indicative rather than verified.

**SGPro** says it outright. The vendor lists "Video or planetary cameras"
among the notable exceptions to its equipment support and states the
product "is designed primarily for Deep Space Object capture"
([SGPro FAQ](https://www.sequencegeneratorpro.com/faq_category/sgpro-help/)).
A COM-marshalled `ImageArray` per exposure is structurally incapable of
sustained high-rate streaming, so this is an architectural boundary, not a
policy a checkbox could reverse.

### What they actually are

Anyone evaluating capture software will meet both, and on their own ground
the comparison runs the other way hard.

**N.I.N.A.** is a free MPL-2.0 deep-sky sequencer covering all eleven
standard ASCOM device classes plus Alpaca Direct plus roughly a dozen
native vendor camera SDK families, with eight plate-solver back-ends, three
guider integrations plus direct pulse guiding, two autofocus measurement
methods, automated meridian flip, a framing assistant, a flat wizard, and
**109 third-party plugins** composed at runtime through MEF. Stable is
**3.2 (2025-11-12)**, with 3.3 in nightly migrating to .NET 10. Its costs
show in its own tracker: a 62 MP camera on a 37-hour session reached 13.8
GB private memory with a stuttering sequencer tree, and one 122 MB XISF
frame took 3.5 to 4 minutes to finalize, which the maintainer declined to
treat as a bug ([issue #417](https://github.com/isbeorn/nina/issues/417)).

**SGPro** is a paid Windows deep-sky sequencer with a strong
unattended-observatory story: equipment profiles, recovery mode, and an
HTTP API self-documenting at `localhost:59590`. It has **no plugin
architecture at all**, and its 2026 train (4.5 on 22 March 2026 plus six
maintenance builds through 4.5.0.1652 on 3 August 2026) is overwhelmingly
plate-solver plumbing and bug fixes, with one genuinely new capability in
the year. It remains on .NET Framework 4.7.2/4.8, a Windows-only terminal
branch, while N.I.N.A. moved to .NET 8 in March 2024. On migration between
the two the honest evidence is thread volume and asymmetry: a Cloudy Nights
thread titled "First time trying out N.I.N.A. instead of SGP" runs past 45
pages, with several more besides, and no corresponding corpus about moving
the other way appears anywhere in the results. No percentage can be
defended from that, and none is offered.

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
> would be unearned: the evidence does not support treating runtime choice
> as a first-order determinant of planetary capture outcomes, because the
> failures in the field are not runtime failures. The weight is not zero
> either, because rendering path, cross-platform reach, memory-safety
> posture, and contribution pool are all real and all traceable to language
> choice. If someone produced a controlled benchmark showing GC-attributed
> frame loss on identical hardware, this weight should rise. Nothing in the
> current record justifies it.

### The scores

| Rank | Product | Throughput (25) | Output (20) | Planetary depth (15) | Hardware (15) | Distribution (15) | Language (10) | **Weighted** |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | **SharpCap** | 8 | 8 | 9 | 9 | 9 | 6 | **8.25** |
| 2 | **FireCapture** | 7 | 8 | 9 | 8 | 6 | 7 | **7.50** |
| 3 | **ASICap** | 9 | 6 | 4 | 3 | 7 | 5 | **6.05** |
| 4 | **RadukCapture** | 6 | 8 | 7 | 1 | 1 | 7 | **5.15** |

**SharpCap** leads on breadth and maintenance at once: roughly 14 native
camera families plus ASCOM plus Alpaca, Sensor Analysis and Smart Histogram
computing optimal gain and exposure from measured read noise and measured
sky background, multi-star FWHM, HFD and Bahtinov focus aids, a
solar-system sequence planner, a working free tier, and an active 4.2 beta
at build 4.2.15166.0 dated 21 September 2026. Its deductions: a stable
release dating to 22 September 2023, a dropped-frame support burden large
enough to warrant its own standing guide thread, and a runtime generation
that could not be established.

**FireCapture** is level on planetary depth and ahead on language. Its
**AutoAlign escalation ladder** (centroid stabilization inside the ROI,
then ROI repositioning, then mount guiding) is the feature most responsible
for its planetary reputation; the frequent confusion is that the base mode
shifts the target within the ROI without moving the mount. It also
autoguides off the main camera, and its ADC tuning aids are a named
strength. It loses on distribution: the 2.7 line has run since 2020 with no
v3, v2.7.15's release date is contested across three candidate dates
spanning March 2025 to February 2026, no release or developer communication
dated in 2026 could be verified from this environment, and commercial use
is prohibited by license. That is an argument from release-date evidence
alone, which is the only evidence there is.

**ASICap** wins outright on raw throughput and loses everything else: ZWO
hardware only, one EAF per instance, no planet centering found, no
documented timestamping, no public issue tracker, and documentation
practitioners describe as extremely lacking. The suite is active, at V1.19
dated 2026-03-31.

**RadukCapture** scores fourth, and the shape of the score is more
informative than the position. The output score of 8 is level with SharpCap
and FireCapture and is earned: correct SER v3 with a 178-byte packed header
under `static_assert`, capture-time timestamps, a per-frame sharpness
sidecar, frame counts patched on close, raw sensor data preserved because
calibration is preview-only by design, and drop accounting exposed in
telemetry ([Recording and SER Format](Recording-and-SER-Format)). The
planetary depth of 7 is real work. The hardware and distribution scores of
1 are not harsh, they are arithmetic, itemized under
[Simply behind](#simply-behind). The throughput score of 6 is generous to a
design that is well reasoned and unmeasured against real silicon.

### The deep-sky ordering, briefly

A deep-sky ranking inverts the field. **N.I.N.A. first** by a wide margin
on feature coverage, plugin ecosystem, device breadth, development
activity, and zero cost. **SGPro second**, maintained, with a strong
unattended-observatory story but a runtime generation behind, no plugin
architecture, and a paid model against a free competitor. **SharpCap
third**, strong at EAA and live stacking with the best polar-alignment
routine in the set, but not a sequencer-first product. **ASIStudio fourth**
via ASIImg, ASILive, and ASIDeepStack: adequate, capped at 16-bit output,
with recurring star-registration failures. **FireCapture and RadukCapture
do not compete at all.** FireCapture "cannot track or align deep-sky
objects", and RadukCapture has no FITS writer, no sequencer, no filter
wheel, no rotator, no meridian flip, and an [autofocus](Autofocus) routine
tuned for a planetary disc.

---

## Convergent architecture, uneven evidence

Strip away the sequencers and the planetary field is three products plus
this one. The engineering convergence is striking: every one is a
producer/consumer pipeline with a pre-allocated in-RAM frame pool draining
to a disk writer, and every one fails the same way when the drain cannot
keep up. SharpCap's developer says it directly: once the buffer fills, "the
capture rate falls to whatever the SSD can absorb, and the camera's frames
are discarded until a memory slot frees up", with buffer occupancy in the
status bar and a Speed Test button that writes a test SER file to measure
the drive's real sustained rate. FireCapture's author describes his RAM
buffer mode in the same terms: "FC pre-allocates frame buffers so the
memory doesn't need to be allocated/released all the time". RadukCapture
does the same with two lock-free SPSC rings in series and deliberately
opposite overflow policies, the 64-slot preview ring overwriting the oldest
unread frame while the write buffer refuses and counts an overrun rather
than ever dropping silently, sized dynamically from available RAM between a
512-slot floor and a 4,096-slot ceiling, about 5 GB and roughly 34 seconds
of absorbed disk stall at 1280x960 RAW8
([`src/core/WriteBuffer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/WriteBuffer.h#L83),
[Capture Pipeline](Capture-Pipeline)).

The throughput evidence is uneven in a way that matters. **ASICap has the
best field numbers in the corpus**: 185 to 186 fps at 2 ms exposure with
zero dropped frames writing directly to disk on an ASI174MM, reported by a
user who had been dropping around 80% of frames in SharpCap, and 170 fps on
an ASI290MM where the same user's FireCapture topped out between 80 and
145. These are single-user, uncontrolled anecdotes with an acknowledged
confound: a SharpCap moderator diagnosed that case as a
`PreviousFrameNotProcessed` pipeline condition, so plausibly a bug rather
than an architectural ceiling. **No controlled head-to-head benchmark of
any two of these applications exists anywhere in the gathered evidence.**

**RadukCapture has no field numbers at all.** Its 1280x960 at 120.3 fps and
147.5 MB/s, and its 3840x2160 at 293 MB/s with zero drops, are emulator
results, and the project says so: the real camera over real USB has never
been measured, the emulator's software render cannot produce 1.5 GB/s, and
overrun behavior under genuine load is explicitly untested
([Emulator and Testing](Emulator-and-Testing)). That gap is the single
largest unknown in this project's competitive position, and it sits on the
heaviest-weighted criterion above.

### Output formats

| Product | Video | Stills |
|---|---|---|
| SharpCap | SER, AVI | PNG, TIFF, FITS, auto-selected at a 5 s exposure threshold |
| FireCapture | SER, AVI (gated by bit depth; 16-bit removes AVI) | FIT, TIF |
| ASICap | AVI (8-bit), SER (16-bit) | none found |
| RadukCapture | SER v3 only | none |

SER-only is defensible for planetary work: it is the format the chain
actually consumes, and its lack of structure is exactly why a capture
application writing it is I/O-bound rather than CPU-bound. The absent FITS
writer is a real gap anyway, because it is the one output that would let
the project touch a deep-sky or photometric workflow. The phase table in
`RadukCapture-Architecture.md` marks a FITS writer shipped and the tree
contains none; where the prose and the code disagree this wiki follows the
code, and this is one of those places. See
[Roadmap and Phase History](Roadmap-and-Phase-History).

---

## SER timestamps: parity with SharpCap, not an advantage over it

Checked against the source, because the convenient story is wrong and had
started to circulate inside the project.

SharpCap's author has acknowledged that **the SER per-frame timestamp is
the frame-arrival time from the camera SDK with no exposure-midpoint
correction**, while FITS output does subtract exposure time, and that "the
SER code hasn't been updated to the better estimate". RadukCapture does
exactly the same thing.
[`src/core/CaptureThread.cpp#L147`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CaptureThread.cpp#L147)
takes the QPC reading via `Timestamp::Now()` immediately after
`GetVideoFrameEx` returns, and
[`src/core/SERWriter.cpp#L87`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/SERWriter.cpp#L87)
maps that onto a wall clock correlated once at `Open()`, writing
`m_ftBase + (frameQPC - m_qpcBaseUs) * 10` into the trailer. That is
frame-arrival time, not exposure midpoint.

> **This is parity, not a differentiator.** What the Phase 59 fix corrected
> was a genuinely worse defect: before it, the trailer recorded disk-write
> time and could lag the photons by up to ten seconds with a full backlog,
> which makes a file useless for WinJUPOS derotation while looking entirely
> valid. Correcting a serious bug brought the product level with the market
> leader. It did not put it ahead, and this wiki should not imply that it
> did.

The open opportunity is small, bounded, and real: subtracting half the
exposure in the writer would make RadukCapture the only product in this
comparison known to stamp exposure midpoints, and it is a handful of lines
in code that already has the exposure value to hand. FireCapture's
per-frame timestamp behavior is unconfirmed, though it does build WinJUPOS
filenames; ASICap's is an open question.

---

## Feature depth across the planetary field

| | SharpCap | FireCapture | ASICap | RadukCapture |
|---|---|---|---|---|
| Video output | SER, AVI | SER, AVI, FIT, TIF | AVI (8-bit), SER (16-bit) | SER v3 only |
| Per-frame SER timestamps | Yes, arrival-time | Unconfirmed; WinJUPOS filenames built in | **Open question** | Yes, arrival-time (parity) |
| Planet centering / tracking | Not evidenced as a planetary feature | AutoAlign, three modes; AutoGuide off the main camera | No equivalent found | Measured 2x2 pulse-to-pixel solve, closed loop |
| Focus aids | Multi-star FWHM, HFD, Bahtinov, Pro auto-scans | Third-party autofocus plugin | Coarse/fine EAF buttons | 11-point V-curve, 3-point parabola, honest revert |
| ADC support | Not evidenced | Tuning aids (named differentiator) | None found | Edlen advisor over fitted filter band; alignment tool dark |
| Hardware breadth | ~14 native families + ASCOM + Alpaca | ~8 native vendors + ASCOM + INDI | ZWO only (camera, EAF, EFW) | 1 camera, 1 focuser, 1 mount family |
| Extensibility | IronPython (Pro-gated; no NumPy/SciPy/AstroPy) | Java `.jar` plugins since v2.5 | None | None |
| Platform | Windows, 64-bit only from 2026 | Windows, macOS, Linux x64, aarch64 | Windows, macOS, Linux | Windows only by construction |
| License / cost | Free + GBP 14/yr Pro (2024 price) | Freeware; commercial use prohibited | Free, ZWO hardware only | PolyForm Noncommercial 1.0.0 |

---

## Language and runtime: what the evidence retires

The analytical heart of the comparison, and it has to survive contact with
evidence that is unflattering to the native-code position.

### Garbage collection: a negative result, carried honestly

Across every practitioner thread, bug report, and vendor statement
gathered, **not one attributes an astronomy-capture frame drop to garbage
collection.** The documented causes, every time, are USB link bandwidth (a
USB3 camera on a USB 2.0 port or behind a shared hub, cables beyond the
roughly 5 m recommendation), sustained disk write throughput, buffer depth
set too small, ROI size, and CPU saturation during save.

The steelman for managed adequacy deserves full strength. A capture
application does not have to process each frame within the inter-frame
interval; it has to drain the queue at the average rate. A ring of 64
pre-allocated 2 MB buffers, 128 MB resident, absorbs a 320 ms stall at 200
fps and loses nothing, which is why the standard field fix for drops in
every one of these products is "increase the buffer". `ArrayPool<T>`,
`GC.AllocateArray(pinned: true)`, and unmanaged allocation make a .NET
capture loop allocation-free in steady state, a pooled `DirectByteBuffer`
does the same on the JVM, and the data path is native anyway because the
vendor SDK DMAs into a native buffer. SharpCap is itself evidence: a .NET
application is the de facto standard capture tool for a large fraction of
the hobby, with no GC-attributed defect visible in its public forum.

Where the managed position breaks down is **the tail, not the mean**. A
ring buffer converts a pause into a latency spike rather than a loss, right
up until the pause exceeds the ring depth. Every planetary frame buffer is
a .NET Large Object Heap object by a factor of 25 to 50, and the LOH is not
compacted by default and is collected only during Gen2
([LOH](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/large-object-heap)).
Fragmentation accumulates over a session rather than over a frame, so an
application clean for the first hour can degrade across a six-hour night,
and Microsoft's own advice in a low-latency mode is to "minimize the number
of new allocations, in particular allocations onto the large object heap
and pinned objects", which is Microsoft documenting that such an
application should avoid exactly what a naive frame-capture loop does.

> **The irreducible point is determinism, not speed.** A C++ ring buffer
> has a provable worst case. A managed one has a worst case that depends on
> the collector, the heap state, the machine, and the runtime version. For
> a hobbyist that is acceptable risk. For occultation timing or lucky
> imaging at the seeing limit it is a real, rarely-exercised limitation.
> That is a values judgment about tolerance for rare catastrophic loss
> dressed as an engineering question, and it should be presented as one.

FireCapture's Java position is visible in its own UI: users size the heap
by hand, and a "Force aggressive memory recovery during capture" toggle is
credited by at least one user with fixing erratic captures. A pre-allocated
buffer mode plus a GC escape hatch plus a user-visible heap knob is the
author designing around the collector rather than the collector solving the
problem for free, and the RAM buffer draws from that heap, so in-RAM
capture duration is capped in a way a 64-bit native application's is not.
Against that, the JVM's pause problem is essentially solved by collector
choice in 2026: ZGC reports sub-millisecond pauses independent of heap
size, well inside a 5 ms frame budget
([JEP 189](https://openjdk.org/jeps/189)). Nothing in the evidence says
which collector FireCapture requests.

### Interop cost, retired with arithmetic

A no-op P/Invoke measures **about 46 ns**. An empty JNI call measures about
**22 ns** on Linux and about **65 ns** in a JMH benchmark on Windows. At
200 fps the frame budget is **5,000,000 ns**, and a capture loop makes one
to five SDK calls per frame: **0.001% to 0.005% of the budget**. Microsoft
documents that there is no marshalling cost between blittable types with
the same representation on both sides. What matters at the boundary is
marshalling semantics, whether the frame crosses by pointer or by copy: a
2 MB per-frame copy at 200 fps is 400 MB/s of pure memcpy. That is
significant, and it is an implementation choice available to be got right
or wrong in any language, not a property of the language. Thread scheduling
is similar. `AvSetMmThreadCharacteristics` is an ordinary `avrt.dll`
export, so no managed application is locked out of the MMCSS Pro Audio
band; the real asymmetry is that an application can prioritize its own
threads but not the GC threads or the JIT. RadukCapture's capture thread
runs at `THREAD_PRIORITY_TIME_CRITICAL` with optional CPU pinning
([`src/core/CaptureThread.cpp#L85`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CaptureThread.cpp#L85)),
which is the right posture and is available to every product here. See
[Threading Model](Threading-Model).

### Where language choice genuinely decides

**Rendering.** Microsoft's own documentation states that GDI+ alpha
blending "is performed by the CPU, so it does not benefit from hardware
acceleration", and a Microsoft benchmark measured approximately **GDI 120
fps, GDI+ 32 fps, and Direct2D hardware 415 fps**
([Comparing Direct2D and GDI](https://learn.microsoft.com/en-us/windows/win32/direct2d/comparing-direct2d-and-gdi)).
That figure is Windows 7-era and test-specific, so use it directionally,
but a GDI+ ceiling below 60 fps before any application work is a real
structural fact for a live planetary preview. RadukCapture's Direct2D path
with a D3D11 compute-shader debayer writing a DXGI-shared texture that D2D
draws directly
([`src/gpu/GPUDebayer.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/gpu/GPUDebayer.h))
is the strongest preview architecture in this comparison, and the clearest
single case where the API choice bought something measurable
([Rendering and UI](Rendering-and-UI)). WPF is more nuanced than "slow": it
is hardware-accelerated in the common case, but the rendering tier is a
property of the user's machine, not of the application.

**Cross-platform reach.** Java's payoff, demonstrated rather than
theoretical: FireCapture ships official Windows x64, Linux, and aarch64
builds from a one-person freeware project, with a Raspberry Pi 5 install
guide using `firecapture_2.7.14_aarch64.deb`. The reach is uneven because
the camera layer is native per-vendor code that does not port for free.
N.I.N.A. is the counter-case: .NET alone does not buy portability if the UI
is WPF, and the response has been a community fork rather than an official
port, with the Linux/macOS issue open and labelled `wontfix`. Pure Win32
plus Direct2D is the most extreme position available, not merely
Windows-only but Win32-only; porting would mean rewriting all 20,809 lines
of `src/gui` plus the GPU and net layers. For planetary that is a coherent
trade, because the workflow is overwhelmingly "laptop beside the
telescope". It is still permanent and irreversible, and should be described
as one.

**Memory safety, where this project has a concrete exposure.** The
convergent empirical figure is about 70%: Chromium's review of 912
high/critical bugs since 2015 found roughly 70% were memory-safety
problems, use-after-free alone at 36%, and Microsoft reported the same
figure across twelve years of security updates. CISA, NSA, FBI, and four
partner agencies published joint guidance in December 2023 urging
manufacturers toward memory-safe languages, with C# and Java on the list
and C++ not. A hobbyist application is not that guidance's threat model,
but the posture applies specifically here: RadukCapture runs a
**hand-rolled RFC 6455 WebSocket server on Winsock2, with hand-rolled SHA-1
and base64 and a masked client-frame parser, accepting mount and focuser
commands with no authentication**
([`src/net/WebSocketServer.cpp`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/WebSocketServer.cpp)).
Attacker-influenceable length fields, masking, fragmentation, and
continuation frames parsed in C++ on a socket is the exact shape of the bug
class that 70% describes, and it originated as CRITICAL finding S1 before
Phase 57 hardening. Loopback-by-default with an explicit `RADUK_WS_LAN=1`
opt-in is the right default
([Telemetry and Dashboard](Telemetry-and-Dashboard)), but the honest
counterweight is that a managed runtime would eliminate the memory-safety
classes and would not have supplied the missing authentication.

**The contribution pool.** N.I.N.A. 3.2 gave each plugin its own
`AssemblyLoadContext` so that "no more dependency clashes between different
plugins" occur, a managed-runtime capability whose Win32/C++ equivalents
are separate processes with IPC or manual DLL version management. That
affordance connects directly to 109 plugins and 78 distinct commit-author
addresses across 8,152 commits. FireCapture's Java plugin API produced a
small but genuine third-party ecosystem. RadukCapture has no plugin
architecture, no extension points, no public API, no CI, and a
single-author commit history, and its **PolyForm Noncommercial 1.0.0
license is what most limits its continuity story**: not an open-source
license under the OSD's no-discrimination criterion, therefore
GPL-incompatible, unpackageable by Linux distributions, and generally
unavailable to contributors whose employers run legal review. What it buys
is precisely targeted, stopping a camera manufacturer from shipping the
capture engine while permitting every amateur user to use, modify, and
redistribute it. If the goal is protection from commercial appropriation it
is the right instrument. If the goal is contributions, MPL-2.0 is the
license that matches, and N.I.N.A. is the working proof in this category.

> **One claim deliberately absent.** No developer-productivity multiplier
> appears anywhere on this page, because no methodologically defensible
> study was found and the figures that circulate here are folklore. The
> defensible version is library-level, not language-level: C#/.NET ships an
> async model, JSON, HTTP and WebSocket stacks, collections, a test
> culture, and a package manager in the box, and a pure-Win32/C++20 project
> must select, vendor, or write each one. This project hand-emits its JSON
> in [`src/net/APEXTelemetry.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/net/APEXTelemetry.h)
> and uses no test framework at all. Where pure Win32 is genuinely weakest
> is **testability**: logic entangled with `WndProc` resists unit testing,
> and this project's own numbers show it, at 13 gated tests linking 5 of 17
> production `.cpp` files and 3,084 test lines against 44,754 source lines.

---

## Where RadukCapture genuinely leads

Three things here are better than what the competition does, and one is
better than anything found in a mainstream capture application.

**The emulator is the clearest technical differentiator in the project.**
[`src/core/CameraEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/CameraEmulator.h)
is 1,316 lines rendering synthetic RAW8/RAW16 frames with per-planet discs,
surface rotation, moons, turbulence, defocus blur, and per-pixel noise, and
[`src/core/MountEmulator.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/MountEmulator.h)
implements the real `IMount` interface and exposes
`GetPointingErrorArcsec()`, which the camera emulator uses to render the
planet displaced from center, so guide pulses shrink the error and the full
`CentroidTracker` correction loop closes in software. Simulator camera
drivers exist for SharpCap and N.I.N.A., but a coupled physical model where
the guide loop closes against rendered pixel displacement is not a standard
feature anywhere in this field. See
[Emulator and Testing](Emulator-and-Testing).

**Guide calibration is measured rather than assumed.**
[`src/core/GuideSolve.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/GuideSolve.h)
pulses east, observes where the centroid went, pulses north, repeats, and
derives a 2x2 matrix from pulse milliseconds to pixels that already
contains the camera's rotation in the focuser, the mirror flip of the
optical train, and the sign of each axis, with a near-zero determinant
detected as parallel responses and the solve refused. This is the approach
PHD2 uses, and it is strictly better than orientation-assuming designs. See
[Tracking and Guiding](Tracking-and-Guiding).

**The [ADC advisor](Advisors) is more physically careful than anything
found in a mainstream capture application.** It implements Edlen (1966)
refractivity and evaluates dispersion between the edges of the band of the
filter actually fitted rather than a fixed 400/700 nm, with site elevation
feeding the pressure term
([`src/core/ADCAssistant.h#L106`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ADCAssistant.h#L106)).
FireCapture's ADC tuning aids are the nearest competitor and are a
different thing: an alignment aid, not a dispersion prediction.

**The [AI assistant](AI-Assistant) is local-only**, streaming to
`http://localhost:11434/api/chat`, with the security review recording
outbound traffic to Ollama on loopback only. The browser dashboard does
call public internet APIs, so "fully local" is true of the application and
the assistant, not of the dashboard.

### Simply behind

None of this is arguable, and it is what the hardware and distribution
scores of 1 are made of.

- **No FITS writer. No AVI.**
- **No polar alignment**, backlogged rather than deferred, because the 8SE
  is alt-az with no wedge.
- **No FWHM or Bahtinov focus aid**, which is what star focus needs and
  what the planetary-disc-tuned V-curve cannot substitute for.
- **No plugin architecture, no scripting, no headless mode, no command
  line.** No multi-camera and no guide camera.
- **One camera, one focuser, one mount family compiled in rather than
  configured**, with camera and focuser swapped by header name rather than
  by interface, so adding a second brand requires a polymorphic abstraction
  that does not exist ([Device Layer](Device-Layer)). Several tuning
  constants are compiled in too: roughly 2000 steps/mm at f/10 in the
  autofocus, an HCG floor of 182 that is an IMX678 property, and a plate
  scale of 0.203"/px that belongs to one scope-camera pair.
- **A 792-line working ASCOM mount implementation whose every known driver
  is blacklisted**, and which this wiki marks Superseded.
- **No installer, no tags, no releases, no signed binary**, output paths
  hardcoded to `D:/RadukCapture/`, and a `cmake_minimum_required(VERSION 4.2)`
  floor that will refuse to configure on most current distributions.
- **Windows-only by construction. No CI**, and 13 tests touching 5 of 17
  `.cpp` files. **No authentication on the remote-control path.**
- **PolyForm Noncommercial** forecloses distribution packaging, corporate
  contributors, and GPL compatibility at once, in a project whose bus
  factor is already 1.

---

## What would change the ranking

In descending order of weighted points returned per unit of work.

1. **Measure the real camera over real USB.** Throughput carries 25% and
   currently rests on emulator figures the project itself disclaims. A
   clear-night FULL RAW8 run with honest drop accounting moves that score
   from 6 toward 8 or 9 and is worth **+0.5 to +0.75** on its own, with no
   code written. It is the cheapest point on this list by a wide margin.
2. **Un-blacklist ASCOM and introduce a polymorphic camera abstraction.**
   Hardware breadth carries 15% at a score of 1, and
   [`src/core/ASCOMMount.h`](https://github.com/Maligzar/Raduk-Capture/blob/master/src/core/ASCOMMount.h)
   is already 792 real lines of `ITelescopeV3` late binding, so the mount
   half is built and switched off. Moving that score to 5 is worth
   **+0.6**, and it is the cheapest large move available.
3. **Ship an installer and tagged releases**, and remove the hardcoded
   `D:/` paths and the CMake 4.2 floor. Distribution carries 15% at a score
   of 1; getting to 5 is another **+0.6** and requires no new subsystem.
4. **Relicense to MPL-2.0, or dual-license.** This moves no score directly,
   but it is the precondition for the contribution pool, the distro
   packaging, and the GPL compatibility that every other continuity
   improvement depends on. N.I.N.A. is the proof it works in this category.
5. **Subtract half the exposure from the SER trailer timestamp.** A bounded
   change in code that already has the exposure value, and it would make
   RadukCapture the only product here known to write exposure midpoints,
   turning today's parity with SharpCap into an actual differentiator for
   WinJUPOS derotation and occultation timing.
6. **Fuzz the WebSocket frame parser and put a token on the command path.**
   The highest-risk component in the application by the standard the
   memory-safety evidence sets. The loopback default is right but is not a
   substitute, and the fix is small.
7. **Write a FITS single-frame writer before an AVI writer.** AVI is a
   legacy container the planetary chain does not want; FITS is the one
   output that would let the product touch a photometric or deep-sky
   workflow, and it would stop the phase table claiming a capability that
   does not exist.

Items 1 through 4 together move the weighted score from **5.15 to roughly
7.2**, past ASICap and within touching distance of FireCapture, and not one
of them requires inventing anything.

---

## What this comparison could not establish

Read this before repeating any vendor-sourced fact from this page.

**The research environment's network policy denied direct access to
firecapture.de, sharpcap.co.uk (including its docs and forums),
zwoastro.com, sequencegeneratorpro.com, nighttime-imaging.eu,
cloudynights.com, and archive.org.** Almost every vendor-sourced claim
above therefore arrived through a search index that read and quoted those
pages server-side, not through a page anyone rendered and read end to end.
Quoted strings are second-hand transcriptions. Prices, build numbers, and
release dates in particular should be re-verified against the live pages
before being relied on: the GBP 14/year SharpCap Pro figure is the 2024
price with no later announcement found, and the SGPro pricing was never
read off a live checkout page.

Two exceptions carry the strongest evidence in the set. **The N.I.N.A.
figures come from a full clone** of `github.com/isbeorn/nina` at commit
`4742afa`, so the file counts, line counts, target frameworks, commit
history, and absent SER/AVI writer are directly measured and reproducible.
**The ZWO SDK findings** came from directly fetched GitHub sources.
RadukCapture's own figures were measured against the working tree on
18 October 2026.

Five things could not be settled. They are listed as open questions rather
than as absences, because inferring them would be inventing them, and
nothing on this page asserts any of them:

1. **ASIStudio's UI toolkit.** No source anywhere names a toolkit in
   connection with the product.
2. **SGPro's UI framework.** The installer prerequisite list naming both
   .NET Framework 3.0-3.5 and 4.7.2 is as consistent with WPF as with
   WinForms.
3. **Whether SharpCap 4.x has moved off .NET Framework 4.8.** The downloads
   page extract says 4.1 "now runs on Microsoft .NET 8 (previously .NET 7)"
   and ships self-contained, while the last developer-sourced requirement
   statement in hand is .NET Framework 4.8 for the 3.3 line. Both are
   second-hand and they are not reconciled.
4. **FireCapture's UI toolkit.** Java is certain from four independent
   angles; the toolkit is not evidenced.
5. **ASICap's timestamping and buffering behavior**, entirely undocumented,
   and bearing directly on the ranking above.

Two claims that circulate about FireCapture are supported by nothing
gathered and appear nowhere in this page's reasoning: that its UI is dated,
and that the community harbors abandonment concerns. Its maintenance
position is argued here from release-date evidence alone.

---

## Documentation drift, named

This comparison measured the tree, and the measurement disagreed with the
project's own prose. The wiki's standard is to follow the code and say so,
which is why these were corrected rather than left: [Home](Home) claimed
"~34,500 lines across 86 source files" against an actual **44,754 lines
across 100 files** (83 `.h` totalling 30,194 and 17 `.cpp` totalling
14,560); "there is no automated test suite" against 13 gated tests; the
emulator documented at 984 lines against 1,316; a bright-star table header
claiming 255 stars against 97; and a phase table marking the FITS writer
shipped when no FITS writer exists. Every prose number in this project
needs verification against the tree before it is repeated.

---

**Next:** [Overview](Overview) · [Capture Pipeline](Capture-Pipeline) · [Roadmap and Phase History](Roadmap-and-Phase-History)
