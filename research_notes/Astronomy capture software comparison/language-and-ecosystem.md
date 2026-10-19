# Language/Runtime Engineering and Ecosystem Standards Context for Astronomy Capture Software

**Research date: 18 October 2026.** Facts are stated as of that date unless an earlier date is given.

**Labelling convention used throughout.** Every bullet carries one of:
- **[SOURCED]** — supported by a cited URL retrieved during this research.
- **[RECALLED]** — from the researcher's prior knowledge, no URL retrieved in this session. Treat as needing verification before publication.
- **[INFERRED]** — a conclusion drawn from the cited findings, not itself directly sourced.
- **[GAP]** — could not be established.

**Tooling limitation that shaped this research (important for the report writer).** In this environment `WebFetch` and direct HTTP both failed (DNS/egress blocked for all target hosts — `ascom-standards.org`, `polyformproject.org`, `spdx.org`, `learn.microsoft.com`, `free-astro.org`, `grischa-hahn.homepage.t-online.de` all returned `ENOTFOUND` or a 403 CONNECT rejection). All findings below come from `WebSearch`, which returns content summaries plus URLs. **I was therefore unable to read any primary-source PDF or full license text verbatim.** Where a quotation appears below it is a quotation as relayed by the search index, not text I read in the original document. The report writer should treat the SER specification PDF, the PolyForm license text, and the CISA/NSA memory-safety document as *needing a direct read before publication*. I have flagged these individually.

---

## Part A1 — Products in scope and their implementation languages

### Takeaway
Five of the six language attributions can be corroborated at least indirectly; **none could be confirmed from a vendor statement of the form "this product is written in X."** N.I.N.A. is the only one with hard, inspectable evidence (public source, published NuGet target frameworks). SharpCap is strongly corroborated as .NET but *not* as C# specifically. FireCapture's Java attribution rests on user-reported Java runtime errors plus its platform matrix. ASIStudio's Qt/C++ attribution could **not** be verified at all.

### Cited Findings

**N.I.N.A. — C# / .NET / WPF, open source. CONFIRMED.**
- N.I.N.A. is open source under the **Mozilla Public License 2.0**; official source at `github.com/isbeorn/nina` — [N.I.N.A. docs](https://nighttime-imaging.eu/docs/master/site), [GitHub isbeorn/nina](https://github.com/isbeorn/nina)
- N.I.N.A. **3.0** (released 18 March 2024) was the release that moved the application to **.NET 8**: "the major change marks the transition to the .NET 8 framework" — [N.I.N.A. 3.0 release note](https://nighttime-imaging.eu/2024/03/18/n-i-n-a-3-0/)
- The published NuGet package `NINA.CustomControlLibrary` (version 3.2.x) targets **`net8.0-windows7.0`**, with `net9.0-windows` and `net10.0-windows` listed as computed-compatible. The package name ("CustomControlLibrary") and the `-windows` TFM are consistent with WPF — [NuGet NINA.CustomControlLibrary](https://www.nuget.org/packages/NINA.CustomControlLibrary)
- N.I.N.A. 3.2 introduced per-plugin **`AssemblyLoadContext`** isolation: "plugins now run in their own AssemblyLoadContexts, meaning no more dependency clashes between different plugins" — [N.I.N.A. release announcement](https://nighttime-imaging.eu/?p=1001). *This is a .NET-specific capability with no Win32/C++ equivalent short of separate processes or manual DLL isolation — see Part A7.*
- Linux fork **PI.N.S.** exists under the same MPL-2.0, retaining attribution to Stefan Berg; a separate unaffiliated community fork **N.I.N.A. Polaris** also exists — [awesome.ecosyste.ms / nina.polaris](https://awesome.ecosyste.ms/projects/github.com%2Fdanwbr%2Fnina.polaris)

**SharpCap — .NET, closed source. PARTIALLY CONFIRMED (.NET yes; C# specifically, no).**
- Author **Robin Glover**; manual copyright "©2018-2023 Robin Glover and David Richards" for the v4.1 manual — [SharpCap 4.1 manual](https://docs.sharpcap.co.uk/4.1/1_Foreword.htm)
- Requirement history, stated by Glover himself in the official forum: SharpCap 3.1 required **.NET Framework ≥ 4.5.2**; 3.2 required **4.6.2**; 3.3 required **4.8** — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=17297), [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=18348)
- Glover, on dependencies: "aside from the camera software itself, the Microsoft .NET framework is the only thing that SharpCap needs" — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=18355)
- **[GAP]** No source found confirming whether SharpCap 4.1/4.2 has moved from **.NET Framework 4.8** (Windows-only, legacy, no longer receiving feature work) to modern **.NET 8/9/10**. This distinction is materially important for the report — see Part A8 on cross-platform reach. The latest sourced data point is .NET Framework 4.8 for 3.3-era builds.
- **[GAP]** No source states SharpCap's source language. ".NET Framework" is consistent with C#, VB.NET, F#, or C++/CLI.

**FireCapture — Java/Swing, closed source, freeware. WEAKLY CONFIRMED (Java yes; Swing specifically, no).**
- Creator **Torsten Edelmann** — [ALPO Astronomy blog, 2010](https://alpo-astronomy.org/computingblog/?p=227)
- Current release line: **v2.7.15, released March 2025**; v2.7 itself released March 2022, v2.7 beta mid-2020 — [firecapture.de](https://firecapture.de)
- Official downloads offered for **Windows (x64), Linux PPA, and aarch64** — [firecapture.de](https://firecapture.de). *This tri-platform-plus-ARM matrix from a single small closed-source vendor is itself strong circumstantial evidence of a JVM-based build.*
- Java dependency evidenced by user-reported failures: a Windows 10 user installing FireCapture 2.7 reported "a Java error" and the program failing to launch — [IceInSpace forum](https://www.iceinspace.com.au/forum/showthread.php?p=1593732); a 2015 thread describes a "Java Platform SE Binary" issue preventing FireCapture from running — [IceInSpace forum](https://www.iceinspace.com.au/forum/showthread.php?p=1164810)
- **[GAP]** No source confirms the UI toolkit is **Swing** rather than SWT or JavaFX, and no source confirms whether a JRE is bundled or must be installed separately.
- Practitioner report on update cadence: an AstroBin user found FireCapture 2.7.14 "doesn't seem to recognise" a ZWO ASI676MC, with a commenter noting "Firecapture doesn't get much updates" — [AstroBin forum](https://app.astrobin.com/forum/topic/133391/asi676mc-and-firecapture). **Practitioner report, not measurement.** Note this is partially contradicted by the March 2025 v2.7.15 release.

**Sequence Generator Pro — .NET/C#/WinForms, closed source. NOT CONFIRMED.**
- **[GAP]** No source retrieved states SGPro's language or UI framework. Searches returned only unrelated NuGet packages named "SequenceGenerator."
- Licensing model confirmed: SGPro moved to an **annually billed subscription**; the standard tier covers **three simultaneous installs**, upgradeable to six; purchasers of **SGPro 2 or 3 retain access to minor-version upgrades without a subscription** — [SGPro official forum announcement](https://forum.sequencegeneratorpro.com/t/important-announcement-for-sgpro/13301)
- **[GAP]** The actual subscription price could not be retrieved (search snippet truncated at the price).

**ASIStudio — C++/Qt, closed source, freeware. NOT CONFIRMED.**
- ZWO describes ASIStudio as a suite of modules: **ASICap, ASIImg, ASILive**, with later manuals adding **ASIDeepStack and ASIFitsView** (five modules) — [ZWO](https://us.zwoastro.com/blogs/product-information/what-is-asistudio-and-what-can-it-bring-us), [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera)
- **ASICap** is the capture module, described by ZWO as usable for "planetary, solar and lunar imaging" and the module where "most camera settings" are set — [ZWO user guide](https://www.zwoastro.com/2021/07/13/a-user-guide-to-asistudio-software-and-the-zwo-asi183mc-camera/3/)
- ASIStudio works **only with ZWO cameras** (reviewer statement) — [ZWO](https://www.zwoastro.com/2020/03/26/what-is-asistudio-and-what-can-it-bring-us/)
- **[GAP]** No source found confirming Qt or C++. ZWO ships ASIStudio for Windows, macOS and Linux (including ARM for ASIAIR-adjacent hardware) **[RECALLED]**, which is consistent with Qt, but this is not sourced.

**Comparison target (Raduk Capture) — C++20, pure Win32 + Direct2D.** Self-reported by the project; not independently verifiable and not in scope for external verification.

### Inferences
- **[INFERRED]** The verification asymmetry is itself a finding worth reporting: the single open-source product in the set (N.I.N.A.) is the only one whose architecture can be audited, benchmarked, or forked by a third party. Every performance claim about SharpCap, FireCapture, SGPro and ASIStudio is necessarily a black-box claim. A comparison that ranks these products on architecture is, for four of six, ranking on inference.
- **[INFERRED]** FireCapture shipping official **aarch64 Linux** binaries alongside Windows and Linux x64, from a one-person freeware project, is the clearest practical demonstration in this product set of the "write once, run anywhere" payoff. Maintaining three architecture targets in pure Win32/C++ would require three separate GUI implementations.

### Gaps
- No vendor-authoritative language statement for SharpCap, FireCapture, SGPro or ASIStudio. All four are closed source; vendors do not publish implementation language.
- Whether SharpCap 4.x runs on .NET Framework 4.8 or modern .NET — unresolved and consequential.
- SGPro: language, UI framework, and current price all unresolved.
- ASIStudio: language and UI framework entirely unresolved.

---

## Part A2 — Garbage-collected runtimes under sustained high-frame-rate capture

### Takeaway
The .NET failure mode for this workload is well documented and specific: **multi-megabyte frame buffers land on the Large Object Heap (LOH), which is not compacted by default and is only collected during Gen2 collections**, so a naive allocate-per-frame capture loop converts every frame into Gen2 pressure. The mitigations (`ArrayPool<T>`, a single large pinned buffer carved into slices, the .NET 5+ Pinned Object Heap, unmanaged allocation) are first-class, documented, and cheap to apply. On the JVM side, ZGC and Shenandoah target sub-millisecond to low-millisecond pauses independent of heap size, which makes modern JVM pause behaviour a non-issue *if the collector is selected and the direct-buffer path is used*; G1, which remains the default, does not meet that bar.

### Cited Findings — .NET

**LOH mechanics (the core of the problem):**
- Objects **≥ 85,000 bytes** are allocated on the LOH — [fablepool.com summary of .NET GC](https://fablepool.com/artifacts/757). *A 1936×1096 8-bit mono frame is ~2.1 MB; a 1920×1200 16-bit frame is ~4.6 MB. Every planetary frame buffer is an LOH object by a factor of 25–50×.*
- "Ordinarily, the large object heap (LOH) isn't compacted because copying large objects imposes a performance penalty." Freed space is reused via a free list — [Microsoft Learn: Large object heap (LOH) on Windows](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/large-object-heap)
- The LOH is collected **only during Gen2 collections** — [dotnet GC memory reference](https://skills.sh/wshaddix/dotnet-skills/dotnet-gc-memory)
- The threshold is configurable via `System.GC.LOHThreshold`, but raising it "usually just moves the problem elsewhere" — [fablepool.com](https://fablepool.com/artifacts/757)
- On-demand compaction: setting `GCSettings.LargeObjectHeapCompactionMode = CompactOnce` makes the next full blocking GC compact the LOH, then reverts to non-compacting — [Microsoft Learn: LOH](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/large-object-heap). Per the `GC.Collect` remarks, this setting "applies to all blocking garbage collections, not just full blocking garbage collections" — [Microsoft Learn: GC.Collect](https://learn.microsoft.com/en-us/dotnet/api/system.gc.collect?view=net-10.0). **A blocking compacting collection of a multi-gigabyte LOH is exactly the stall a capture loop cannot absorb.**
- Automatic LOH compaction occurs when a hard memory limit is set, and when `ConserveMemory` is non-zero and fragmentation is high; in containers on .NET Core 3.0+ the LOH is automatically compacted — [Microsoft Learn: GC config settings](https://learn.microsoft.com/en-us/dotnet/core/runtime-config/garbage-collector), [Microsoft Learn: ASP.NET Core memory](https://learn.microsoft.com/en-us/aspnet/core/performance/memory?view=aspnetcore-10.0)

**Pinning:**
- Pinning an object **on the LOH has no effect on compaction, because the LOH is never compacted**. Pinning on the small object heap is the harmful case, because it blocks compaction there — [Microsoft DevBlogs (2004)](https://devblogs.microsoft.com/dotnet/?p=23154). *This is an old post; the mechanism is unchanged but verify against current docs.*
- Since **.NET 5** the **Pinned Object Heap (POH)** isolates pinned objects so they do not obstruct compaction of other generations — [dotnet GC memory reference](https://skills.sh/wshaddix/dotnet-skills/dotnet-gc-memory). *Allocate via `GC.AllocateArray<T>(length, pinned: true)`.* **[RECALLED]** for the API name.

**Mitigations Microsoft itself recommends:**
- Microsoft's own .NET 4.5 LOH guidance recommends reusing buffers through an **object pool**, which "means fewer LOH allocations and collections and less fragmentation" — [Microsoft DevBlogs: LOH improvements in .NET 4.5](https://devblogs.microsoft.com/dotnet/?p=1293)
- `ArrayPool<byte>.Shared.Rent` is the standard modern mechanism — [dotnet GC memory reference](https://skills.sh/wshaddix/dotnet-skills/dotnet-gc-memory)
- The canonical I/O pattern: allocate **one** large buffer, slice it into chunks, reuse it — fragmentation is avoided because only one immovable buffer lives on the LOH — [Ayende Rahien](https://ayende.net/blog/170243/long-running-async-and-memory-fragmentation). *Old post; pattern still correct.*
- Fragmentation anecdote: one source reports LOH fragmentation reaching **50–80% of LOH size** in long-running services — [fablepool.com](https://fablepool.com/artifacts/757). **Explicitly anecdotal, not a benchmark.** Treat as illustrative only.

**Workstation vs Server GC, and pause characteristics:**
- Background (concurrent) GC's primary benefit is shortening long **Gen2** pauses. One Microsoft Developer Support measurement found that enabling background workstation GC reduced the **number of pauses over 200 ms from 11 to 2**, "mainly because Gen2 time fell" — [Microsoft DevBlogs: Understanding different GC modes with Concurrency Visualizer](https://devblogs.microsoft.com/premier-developer/understanding-different-gc-modes-with-concurrency-visualizer/). **Caveat: that post is roughly a decade old; the numbers are dated and workload-specific.**
- In the same measurements, **server GC** reduced total GC pause time to **~9% of run time** versus **~35%** for workstation GC — [same source](https://devblogs.microsoft.com/premier-developer/understanding-different-gc-modes-with-concurrency-visualizer/)
- Server GC trade-off, per Microsoft: per-heap dedicated threads and larger heaps mean "memory saturation happens less frequently, but once it happens, the CLR needs to do more work to traverse the heap" — i.e. **fewer but longer pauses** — [same source](https://devblogs.microsoft.com/premier-developer/understanding-different-gc-modes-with-concurrency-visualizer/)
- Since .NET Framework 4.5, **background server GC** offloads most full-blocking-collection work to dedicated background threads running concurrently with user code, "resulting in much shorter (less noticeable) pauses" — [Microsoft DevBlogs: .NET 4.5 GC enhancements](https://devblogs.microsoft.com/dotnet/the-net-framework-4-5-includes-new-garbage-collector-enhancements-for-client-and-server-apps/)
- Background **workstation** GC uses **one** dedicated background GC thread; background **server** GC uses **multiple** — [Microsoft Learn: Background garbage collection](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/background-gc)
- Microsoft's general guidance: "if your application uses multiple threads and object instances, use server garbage collection instead of workstation garbage collection" — [Microsoft Learn: Garbage Collection and Performance](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/performance)

**Latency modes:**
- `GCLatencyMode.LowLatency` "suppresses generation 2 collections and performs only generation 0 and 1 collections." **It is available only for workstation GC** — [Microsoft Learn: Latency Modes](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency)
- `SustainedLowLatency` "suppresses foreground generation 2 collections and performs only generation 0, 1, and background generation 2 collections." Available for both workstation and server GC. Because it does not compact the managed heap, **higher fragmentation is possible**, and it "results in a larger managed heap size than other modes" — [Microsoft Learn: Latency Modes](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency)
- Microsoft's explicit advice while in low-latency mode: "minimize the number of new allocations, in particular allocations onto the large object heap and pinned objects" — [Microsoft Learn: Latency Modes](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency). **This is Microsoft stating, in its own documentation, that a low-latency .NET application should avoid doing exactly what a naive frame-capture loop does.**
- **.NET 10** introduced **DATAS**, which "can both limit the memory usage and make the GC pauses lower with more GC threads doing the collection work" — [Microsoft DevBlogs: Preparing for the .NET 10 GC (DATAS)](https://devblogs.microsoft.com/dotnet/preparing-for-dotnet-10-gc/)
- **[RECALLED]** `GC.TryStartNoGCRegion(long totalSize)` lets an application request a window in which no GC occurs, provided the requested budget can be satisfied up front. Plausible for a bounded capture run; no URL retrieved. Verify before publication.

### Cited Findings — JVM

- **ZGC**: stop-the-world pauses "typically under 1 millisecond regardless of heap size" — [foojay.io: GC algorithms G1, ZGC and Shenandoah](https://foojay.io/pedia/gc-algorithms-g1-zgc-and-shenandoah/). **Secondary source.**
- **Shenandoah**: JEP 189 states pause times are **independent of heap size** — [OpenJDK JEP 189](https://openjdk.org/jeps/189). *This is a stated design goal in the JEP, not a measurement.* A 2026 comparison places Shenandoah in the "low millisecond" range versus sub-millisecond for ZGC, and notes Shenandoah's generational mode still has stop-the-world pauses — [space-node.net, 2026](https://space-node.net/blog/shenandoah-vs-zgc-minecraft-garbage-collector-2026). **Low-quality secondary source (Minecraft server tuning blog); use only directionally.**
- **G1**: typical pauses reported at **20–200 ms**, up to ~500 ms on large heaps — [space-node.net, 2026](https://space-node.net/blog/shenandoah-vs-zgc-minecraft-garbage-collector-2026). A separate 2025 AWS benchmark found **G1 p95 pauses around 750 ms** on one workload — [oneuptime.com GC tuning](https://oneuptime.com/blog/post/2026-01-30-gc-tuning-best-practices/view). **Different workloads, not comparable; both secondary.**
- Both ZGC and Shenandoah are **generational** as of JDK 25, with "minimal" performance differences between them — [oneuptime.com](https://oneuptime.com/blog/post/2026-01-30-gc-tuning-best-practices/view), [space-node.net](https://space-node.net/blog/shenandoah-vs-zgc-minecraft-garbage-collector-2026). **Secondary; verify against JDK 25/26 release notes.**
- Generational Shenandoah was previewed via AWS/Amazon Corretto work — [AWS Developer Tools Blog](https://aws.amazon.com/blogs/developer/announcing-preview-release-for-the-generational-mode-to-the-shenandoah-gc)

**Off-heap / DirectByteBuffer:**
- Direct buffers are allocated **outside the Java heap in native memory**; a small on-heap object represents each one, and the native block is freed only when the GC collects that object — [eBay SRE case study](https://innovation.ebayinc.com/stories/sre-case-study-triage-a-non-heap-jvm-out-of-memory-issue/), [DZone](https://dzone.com/articles/troubleshooting-problems-with-native-off-heap-memo)
- **Critical detail for capture code:** the OS can only read/write native memory, so a heap buffer used for I/O is **copied into a temporary direct buffer the JDK creates internally**, cached per-thread with **no default size limit** — [DZone](https://dzone.com/articles/troubleshooting-problems-with-native-off-heap-memo). *Writing SER frames from on-heap `byte[]` therefore incurs a hidden native copy per write; using `DirectByteBuffer` end-to-end eliminates it.*
- Operational hazard: `OutOfMemoryError: Direct buffer memory` from hitting `-XX:MaxDirectMemorySize`, **which is unset by default** — [eBay SRE case study](https://innovation.ebayinc.com/stories/sre-case-study-triage-a-non-heap-jvm-out-of-memory-issue/)
- Guidance: **reuse or pool direct buffers** rather than allocating per request; allocation is slower and constant allocation produces GC churn and page faults — [Cleverence/Oracle ByteBuffer docs summary](https://www.cleverence.com/amp/articles/oracle-documentation/bytebuffer-java-platform-se-8-4837/)

### Inferences
- **[INFERRED]** The structural point for the report: **the identical mitigation exists on both managed runtimes, and it is the same mitigation a C++ developer applies by default.** `ArrayPool<T>`/POH on .NET and pooled `DirectByteBuffer` on the JVM are both "allocate a fixed ring of frame buffers once at stream start and never allocate again." A competent managed implementation and a C++ implementation converge on the same memory architecture. The difference is that in C++ that architecture is the path of least resistance, and on a managed runtime it is a deliberate deviation from idiomatic code that a developer must know to make.
- **[INFERRED]** `GCLatencyMode.LowLatency` being **workstation-GC-only** while Microsoft simultaneously recommends **server GC** for multithreaded applications is a genuine tension for a capture app: the configuration that best suppresses Gen2 is unavailable in the configuration Microsoft otherwise recommends. `SustainedLowLatency` is the compromise, at the cost of a larger heap and more fragmentation.
- **[INFERRED]** On the JVM, the pause question is essentially **solved by collector choice** in 2026 — ZGC at sub-millisecond is comfortably inside a 5 ms frame budget at 200 fps. The residual JVM risk for this workload is not pause time but the **hidden heap→direct-buffer copy on I/O** and the unbounded direct-memory default.

### Gaps
- **No measurement of GC pause behaviour in any astronomy capture application.** I found no profile, trace, or benchmark of N.I.N.A., SharpCap or FireCapture under sustained high-fps capture. Every GC claim in this section is from general runtime documentation, applied to this workload by reasoning.
- The current JDK's default collector and which collector FireCapture requests (if any) — unknown. FireCapture ships no documented JVM flags in anything I retrieved.
- ZGC/Shenandoah pause figures come only from secondary 2026 blogs, not from OpenJDK's own measurements.

---

## Part A3 — Is GC the binding constraint? (the central question)

### Takeaway
**The evidence leans clearly toward "no, GC is not the binding constraint" — the binding constraints are USB3 link bandwidth, sustained disk write throughput, and per-frame CPU work (debayer, preview, overlay).** The single most directly relevant practitioner data point found is that **ASICap sustained 185–186 fps with zero dropped frames to disk on hardware where SharpCap dropped frames** — but the SharpCap maintainer's own diagnosis of a comparable report pointed at a `PreviousFrameNotProcessed` condition, i.e. a **pipeline/scheduling** problem, not a GC problem. The honest position: managed runtimes are adequate for this workload *when written to pool buffers*, and the failures that show up in the field are overwhelmingly attributable to architecture, buffering and the capture pipeline rather than to the collector. Where a managed runtime genuinely breaks down is in the **tail**, not the mean.

### Cited Findings

**Bandwidth arithmetic — the first-order constraint:**
- Vendor guidance gives the formula `Data Rate = Resolution × Frame rate × bit depth`, and works a concrete example: **1920×1200 at 150 fps ≈ 345 MB/s**, enough to overwhelm a laptop driving more than one such camera — [Swing Catalyst USB3 camera troubleshooting](https://support.swingcatalyst.com/hc/en-us/articles/5516849894930-USB3-camera-troubleshooting)
- The same guide states a laptop "may not be able to drive two full HD high FPS USB3 cameras" — [same source](https://support.swingcatalyst.com/hc/en-us/articles/5516849894930-USB3-camera-troubleshooting)
- Recommended maximum USB3 cable length **5 metres**; dropped frames commonly caused by a USB3 camera on a USB 2.0 port, poor/extender cables, or a shared hub — [same source](https://support-new.swingcatalyst.com/en/cameras-video/troubleshooting/usb3-troubleshooting/)

*[INFERRED from the above] USB 3.0's 5 Gbit/s signalling gives roughly 400–450 MB/s of practical bulk throughput. 345 MB/s for a single 1920×1200 8-bit 150 fps stream is therefore already ~80% of the link. At that point the link, not the runtime, sets the ceiling — and the application's job is to not be the thing that fails first.*

**The decisive practitioner comparison (ASICap vs SharpCap):**
- A user with a fast SSD reported fps dropping to **36 fps while recording**, with **32,000 frames dropped after 5,000 frames saved**. The SharpCap moderator identified the log cause as **`PreviousFrameNotProcessed`** — not a drive bottleneck — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=49196)
- The same user then **switched to ASICap and captured 185–186 fps with zero dropped frames directly to disc**, which was read as suggesting a SharpCap bug — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=49196). **Practitioner report, single case, not a controlled measurement.**
- Another user reported getting a **full 60 fps preview but only 40 fps when capturing**, because they had to reduce the camera's USB traffic setting to stop dropped frames — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=36527). **Practitioner report.**
- Reducing ROI is the standard field fix: one lunar imager "didn't drop frames at all when reducing the ROI to 1280×720, no matter how many frames were captured" — [IceInSpace forum](https://www.iceinspace.com.au/forum/showthread.php?p=1226101). **Practitioner report.**
- Several users resolved drops by **increasing the software frame buffer** or lowering the camera's USB traffic setting — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=11210). **Practitioner report.**

**Microsoft's own position on what to do in a low-latency managed app** (bears directly on the steelman): "minimize the number of new allocations, in particular allocations onto the large object heap and pinned objects" — [Microsoft Learn: Latency Modes](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency)

### Inferences — the steelman, and where it breaks

**[INFERRED] The steelman for "a managed runtime is perfectly adequate here":**
1. **The pause budget is not as tight as it looks.** At 200 fps the inter-frame interval is 5 ms, but a capture application does not have to process each frame within 5 ms — it has to *drain the queue at the average rate*. With a ring of, say, 64 pre-allocated 2 MB buffers (128 MB resident), the application can absorb a **320 ms** stall and lose nothing. A background Gen2 collection that pauses user threads for tens of milliseconds is invisible against that buffer. This is the single strongest argument, and it is the reason the SharpCap field fix for drops is "increase the buffer."
2. **The mitigations are complete, not partial.** `ArrayPool<T>` + `GC.AllocateArray(pinned: true)` on the POH + unmanaged `Marshal.AllocHGlobal` for camera DMA targets means a .NET capture loop can be made **allocation-free in steady state**. A collector that is never triggered cannot pause. Microsoft documents all three mechanisms. ([Microsoft DevBlogs LOH](https://devblogs.microsoft.com/dotnet/?p=1293), [dotnet GC reference](https://skills.sh/wshaddix/dotnet-skills/dotnet-gc-memory))
3. **The real work is in native code anyway.** The camera SDK (ASI/QHY/Player One) DMAs into a native buffer; the debayer and the disk write can both be done on native memory. The managed layer is then orchestration, not data path — and orchestration happens at tens of Hz, not hundreds.
4. **The field evidence does not implicate GC.** The documented SharpCap drop causes in the threads above are USB port/cable, USB traffic setting, ROI, buffer depth, and a `PreviousFrameNotProcessed` pipeline condition. **I found no practitioner report, bug, or measurement attributing astronomy-capture frame drops to garbage collection.** ([SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=49196))
5. **SharpCap's own commercial success is evidence.** A .NET application is the de facto standard capture tool for a large fraction of the hobby, sustaining high frame rates for a paying user base, with no GC-attributed defect visible in its public forum.

**[INFERRED] Where it genuinely breaks down:**
1. **The tail, not the mean.** A ring buffer converts a pause into a latency spike rather than a loss — until the pause exceeds the ring depth. The LOH is the specific hazard: a **compacting blocking** LOH collection (triggered by `CompactOnce`, by a hard memory limit, or by `ConserveMemory` under high fragmentation — all documented at [Microsoft Learn: GC config](https://learn.microsoft.com/en-us/dotnet/core/runtime-config/garbage-collector)) on a heap holding hundreds of megabytes of frame buffers is a *copy* of all that memory. That is not a tens-of-milliseconds event. This is a rare-but-catastrophic tail, and it is precisely the event that ruins a 3-minute Jupiter run at the moment of best seeing.
2. **Fragmentation accumulates over a session, not over a frame.** The LOH is non-compacting and collected only at Gen2 ([Microsoft Learn: LOH](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/large-object-heap), [dotnet GC reference](https://skills.sh/wshaddix/dotnet-skills/dotnet-gc-memory)). If buffer sizes vary — which they do, every time the user changes ROI or binning mid-session — the free list fragments. A capture app that is fine for the first hour can degrade over a six-hour session. This failure is hard to reproduce and hard to attribute, which is consistent with the observed pattern of unexplained, intermittent field reports.
3. **`SustainedLowLatency` trades the pause for the fragmentation.** It "does not compact the managed heap, [so] higher fragmentation is possible" and "results in a larger managed heap size" ([Microsoft Learn: Latency Modes](https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/latency)). You cannot have both.
4. **Determinism is not available, only probability.** A C++ ring buffer has a provable worst case. A managed ring buffer has a worst case that depends on the collector, the heap state, the machine, and the runtime version. For a hobbyist this is an acceptable risk; for anything with a hard deadline (occultation timing, lucky-imaging at the seeing limit) the inability to *bound* the tail is a real, if rarely-exercised, limitation.
5. **It raises the floor on developer skill.** The mitigations work but must be known. An idiomatic C# capture loop that returns a fresh `byte[]` per frame is both the natural thing to write and the worst possible thing to write. C++ does not have an equivalently attractive wrong default.

**[INFERRED] Where the engineering community disagrees, and which way the evidence leans.** The practitioner community broadly treats capture performance as a hardware/configuration problem (USB port, cable, ROI, buffer, USB traffic) and does not discuss GC. The systems-programming community treats managed runtimes as categorically unsuitable for soft-real-time data paths. **The evidence found here leans toward the practitioners for the mean case and toward the systems programmers for the tail case.** The honest framing is: GC is not the constraint on *throughput*; it is a contributor to *worst-case jitter*, and the significance of worst-case jitter depends entirely on whether you think losing a run occasionally is acceptable. This is a values question dressed as an engineering question, and the report should present it that way rather than resolving it.

### Gaps
- **No controlled measurement exists** comparing a managed and a native capture application on identical hardware with the same camera. The ASICap-vs-SharpCap report ([SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=49196)) is one user, one setup, uncontrolled, and the confound (SharpCap may simply have had a bug) is acknowledged in the thread itself.
- No published GC trace or ETW profile of any astronomy capture application.
- Debayer and preview cost per frame: no measurement found for any product. Whether debayer is done on CPU or GPU in each product is unknown.

---

## Part A4 — Native-SDK interop costs

### Takeaway
Per-call interop overhead on both managed runtimes is **tens of nanoseconds** for a trivial call, which is three to four orders of magnitude below the per-frame budget at 200 fps (5 ms). **Interop cost is not a factor at these frame rates for a sanely structured API** — the camera SDKs expose per-frame calls, not per-pixel calls. The cost only becomes relevant if an implementation crosses the boundary per-row or per-pixel.

### Cited Findings
- **P/Invoke, measured:** approximately **46 ns** for a simple no-op P/Invoke call on .NET for Windows — [gRPC PInvokeTest.cs comment](https://fuchsia.googlesource.com/third_party/grpc/+/cobalt/src/csharp/Grpc.Core.Tests/PInvokeTest.cs)
- **P/Invoke, documented:** "PInvoke has an overhead of between 10 and 30 x86 instructions per call. In addition to this fixed cost, marshaling creates additional overhead." Critically: "There is no marshaling cost between blittable types that have the same representation in managed and unmanaged code" — [Microsoft: Calling Native Functions from Managed Code](https://learn.microsoft.com/en-us/previous-versions/ms235282(v=vs.140))
- **JNI, measured (2022, Linux):** empty JNI call **≈ 22 ns**; a real `getloadavg` call **≈ 470 ns** — [dkomanov, Java Native Access performance](https://dkomanov.medium.com/java-native-access-performance-cf4ce0d68ddb)
- **JNI vs Panama, measured (2019, JMH, Windows):** `jni_javacpp` **64.958 ± 3.608 ns/op**; `panama` **39.231 ± 1.951 ns/op`; `GetSystemTime` via JNI **≈ 296 ns**. The author notes the Panama path lacked Windows optimizations at the time — [OpenJDK panama-dev mailing list](https://mail.openjdk.org/pipermail/panama-dev/2019-May/005450.html)
- **JNI, older (2008):** ~35 cycles for a native method call with four `jint` parameters on a Core 2 Duo; the same thread notes JNI overhead remained a bottleneck for small-primitive calls — [OpenJDK discuss list](https://mail.openjdk.org/pipermail/discuss/2008-March/000690.html)
- Universal guidance across both platforms: prefer **fewer, larger** calls over many small ones — [Microsoft](https://learn.microsoft.com/en-us/previous-versions/ms235282(v=vs.140))

### Inferences
- **[INFERRED]** At 200 fps the per-frame budget is 5,000,000 ns. A camera SDK capture loop makes on the order of 1–5 interop calls per frame (`GetVideoData`, perhaps a status poll). At ~46 ns each that is **~0.005% of the frame budget**. **Interop overhead is irrelevant at this scale and the report should say so plainly** — it is a commonly cited objection to managed runtimes that does not survive arithmetic for this workload.
- **[INFERRED]** The relevant interop question is not *cost* but *marshalling semantics*: whether the frame buffer crosses the boundary **by pointer** (blittable, zero-copy — Microsoft confirms no marshalling cost for blittable types) or **by copy**. A 2 MB per-frame copy at 200 fps is 400 MB/s of pure memcpy — that *is* significant, and it is an implementation choice, not a language property. Both .NET (via `IntPtr`/`Span<T>` over pinned or unmanaged memory) and Java (via `DirectByteBuffer`, which the camera can DMA into) can avoid it.
- **[INFERRED]** Java's position improved materially with the **Foreign Function & Memory API** (Project Panama), measured ~40% faster than JNI even in its unoptimized 2019 state. **[RECALLED]** FFM finalised in JDK 22 (JEP 454). A Java application still using hand-written JNI in 2026 is carrying avoidable cost and avoidable unsafety.
- **[INFERRED]** Direct C++ linkage is a normal function call — single-digit nanoseconds or inlined away. The gap versus managed interop is real but, at these call frequencies, immeasurable in practice.

### Gaps
- No head-to-head P/Invoke-vs-JNI benchmark on identical hardware exists in the sources found; the numbers above are from different years, OSes and CPUs and are only loosely comparable.
- Whether any specific product copies frame buffers across the interop boundary — unknown for all closed-source products.

---

## Part A5 — Real-time thread scheduling on Windows

### Takeaway
MMCSS gives a Win32 application access to priorities **23–26** ("Pro Audio" class), above the 16–22 band used for normal foreground work. This is a **documented Win32 API** (`AvSetMmThreadCharacteristics`) and is callable from managed code via P/Invoke, so it is not an exclusively native capability — but the GC's own threads are not under the application's scheduling control, which is the real asymmetry.

### Cited Findings
- MMCSS's **High** category, "designed for Pro Audio tasks," runs threads at **priorities 23–26**; the **Medium** category (**16–22**) covers the foreground application's threads. "By default, the priority of a 'Pro Audio' thread is higher than that of an 'Audio' thread" — [Microsoft Learn: Multimedia Class Scheduler Service](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service)
- Threads opt in by calling **`AvSetMmThreadCharacteristics`** with a task name — [Microsoft Learn: MMCSS](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service)
- WASAPI automatically assigns the "Pro Audio" task name to threads servicing exclusive-mode streams with a device period under 10 ms — [Microsoft Learn: Exclusive-Mode Streams](https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams)
- **MMCSS does not change the system timer.** Microsoft's guidance: applications "should not set the resolution of the system-wide timer to a small value unless absolutely necessary to conserve power" — [Microsoft Learn: MMCSS](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service)
- Raising timer resolution (e.g. to 1 ms) "affects the global system timer resolution, which can impact overall system performance and increase energy consumption" — [Microsoft Q&A: Why WinMM timers API is obsolete?](https://learn.microsoft.com/en-us/answers/questions/5693199/why-winmm-timers-api-is-obsolete)
- The legacy MMCSS registry **"Clock Rate"** hint was **removed starting with Windows 7 / Server 2008 R2** to reduce power consumption — [Microsoft Learn: MMCSS](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service)
- Media Foundation exposes MMCSS registration to higher-level components via `IMFRealTimeClientEx::RegisterThreadsEx` and the `MF_READWRITE_MMCSS_CLASS` / `MF_READWRITE_MMCSS_PRIORITY_AUDIO` attributes — [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nf-mfidl-imfrealtimeclientex-registerthreadsex), [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-readwrite-mmcss-class)

### Inferences
- **[INFERRED]** `AvSetMmThreadCharacteristics` is an ordinary `avrt.dll` export and is P/Invokable from C# and callable from Java via JNI/FFM. **A managed application is not locked out of MMCSS.** Any claim that only a native application can raise a capture thread into the Pro Audio band is false.
- **[INFERRED] The genuine managed-runtime limitation is different and more subtle:** the application can set the priority of *its own* threads, but it cannot control the priority, affinity or timing of the **GC threads**, the **finalizer thread**, or the **JIT compiler thread**. A background GC thread contending with an MMCSS-elevated capture thread is a scheduling interaction the application did not author and cannot tune. On .NET this is partly addressable (`GCHeapAffinitizeMask`, `GCNoAffinitize` — **[RECALLED]**, no URL retrieved), but only partly.
- **[INFERRED]** A second managed-specific hazard: **tiered JIT recompilation**. A hot capture loop may be re-JITted mid-session, producing a one-off stall at an unpredictable moment. `TieredCompilation=false` or ReadyToRun/AOT addresses it. **[RECALLED]** — no URL retrieved; verify.
- **[INFERRED]** Microsoft's own advice *against* raising the global timer resolution cuts against the common capture-app practice of calling `timeBeginPeriod(1)`. The modern alternative is a waitable timer with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` **[RECALLED]** — verify before publication.

### Gaps
- No source found on whether any astronomy capture product actually registers with MMCSS. This is unverifiable for the closed-source products and I did not inspect N.I.N.A.'s source.
- Current Windows 11 timer-resolution behaviour (per-process timer resolution changes introduced around Windows 10 2004) — not retrieved.

---

## Part A6 — UI rendering: WPF, WinForms/GDI+, Swing, Qt, Direct2D

### Takeaway
There is a clear documented performance ordering for 2D rendering on Windows: **Direct2D > GDI > GDI+**, with GDI+ roughly **an order of magnitude slower than Direct2D** in Microsoft's own (dated, Windows 7-era) test. WPF is DirectX-backed but has documented **software fallback paths that are never hardware accelerated**. The practical consequence for a 60+ fps live preview with overlays: WinForms/GDI+ is the weakest position, because alpha blending and anti-aliasing are explicitly CPU-bound.

### Cited Findings

**Direct2D vs GDI/GDI+:**
- Microsoft: "Direct2D outperforms GDI and GDI+ in most situations" — [Microsoft Learn: Overview of the Windows Graphics Architecture](https://learn.microsoft.com/en-us/windows/win32/learnwin32/overview-of-the-windows-graphics-architecture)
- "Direct2D is layered on top of Direct3D, and takes full advantage of hardware acceleration provided by the GPU. If the GPU does not support the features needed for Direct2D, then Direct2D falls back to software rendering" — [Microsoft Learn: Comparing Direct2D and GDI](https://learn.microsoft.com/en-us/windows/win32/direct2d/comparing-direct2d-and-gdi)
- **The key GDI+ limitation:** "GDI+ supports transparency, but the alpha blending is performed by the CPU, so it does not benefit from hardware acceleration" — [Microsoft Learn: Comparing Direct2D and GDI](https://learn.microsoft.com/en-us/windows/win32/direct2d/comparing-direct2d-and-gdi)
- Microsoft claims that even without a GPU, Direct2D's software rasterizer beats GDI+: "applications that use Direct2D experience substantially better rendering performance than with GDI+ and with similar visual quality" — [Microsoft Learn: About Direct2D](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-overview). **Vendor claim, not independent measurement.**
- **Benchmark (Windows 7 era, MSDN blog):** approximately **GDI 120 fps, GDI+ 32 fps, Direct2D hardware 415 fps** (≈160 fps with anti-aliasing enabled). The author notes anti-aliasing is expensive for Direct2D too — [MSDN: Introducing the Microsoft Direct2D API](https://learn.microsoft.com/en-us/archive/blogs/thomasolsen/introducing-the-microsoft-direct2d-api). **Dated and test-specific; use directionally only.**
- Both GDI and GDI+ "continue to be supported in Windows," but "Direct2D and DirectWrite are recommended for new programs" — [Microsoft Learn: Overview of the Windows Graphics Architecture](https://learn.microsoft.com/en-us/windows/win32/learnwin32/overview-of-the-windows-graphics-architecture)
- Direct2D interoperates with GDI/GDI+/Direct3D, enabling incremental migration — [Microsoft Learn: About Direct2D](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-overview)

**WPF rendering tiers:**
- **Tier 0** = no hardware acceleration, all graphics features use software rendering, DirectX level below 9.0 — [Microsoft Learn: Graphics Rendering Tiers](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/graphics-rendering-tiers)
- Since .NET Framework 4, **Tier 1 and Tier 2 both require DirectX ≥ 9.0**; Tier 2 additionally requires more video RAM plus vertex shader and multitexture support. Hardware supporting only DirectX 7/8 was reclassified from Tier 1 to Tier 0 — [Microsoft Learn: Graphics Rendering Tiers](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/graphics-rendering-tiers). **Note: an older WPF performance page still gives the pre-.NET-4 definitions (Tier 1 = DX 7.0–8.x); the .NET 4+ definitions supersede it** — [Microsoft Learn (legacy)](https://learn.microsoft.com/en-us/previous-versions/dotnet/netframework-4.0/ms742196(v=vs.100))
- **Never hardware accelerated in WPF:** all printed content ("All printed content is rendered using the WPF software pipeline"), `RenderTargetBitmap` rendering, and tiled `TileBrush` content — [Microsoft Learn: Optimizing Performance: Taking Advantage of Hardware](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/optimizing-performance-taking-advantage-of-hardware)
- Layered windows are software-rendered on pre-WDDM systems — [Microsoft Learn: Optimizing Performance](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/optimizing-performance-taking-advantage-of-hardware)
- Applications read the tier at runtime: `int renderingTier = (RenderCapability.Tier >> 16);` — [Microsoft Learn: RenderCapability.Tier](https://learn.microsoft.com/en-us/dotnet/api/system.windows.media.rendercapability.tier?view=windowsdesktop-10.0)
- Software rendering can be forced per-process (`RenderOptions.ProcessRenderMode = RenderMode.SoftwareOnly`, .NET 4+) or per-window (`hwndTarget.RenderMode`, .NET 3.5 SP1+) — [MSDN: Software Rendering Usage in WPF](https://learn.microsoft.com/en-us/archive/blogs/jgoldb/software-rendering-usage-in-wpf)

### Inferences
- **[INFERRED] Ranking for a 60+ fps live preview with overlays:**
  - **Direct2D/Direct3D (Raduk Capture):** best case. GPU-composited, hardware alpha, direct swap-chain presentation, no managed marshalling of the frame. The developer owns the entire pipeline and the per-frame cost is bounded.
  - **Qt (ASIStudio, unverified):** Qt's `QPainter` can target OpenGL/Direct3D via RHI, and Qt Quick is scene-graph/GPU-based **[RECALLED]**. Competitive if the GPU path is used; closer to GDI+ if a raster `QWidget` path is used. No source retrieved.
  - **WPF (N.I.N.A.):** DirectX-backed retained-mode, so hardware-accelerated in the common case — but retained-mode is a poor structural fit for replacing a full-frame bitmap 60+ times a second, and the documented software-only paths (notably `RenderTargetBitmap`, which is the obvious way to snapshot a preview) are traps. Also: a WPF `WriteableBitmap` update crosses the managed/unmanaged boundary and interacts with the dispatcher.
  - **Swing (FireCapture):** Java2D with an OpenGL or D3D pipeline available but historically not default on Windows **[RECALLED]**; single-threaded EDT. Likely the weakest of the GPU-capable options. No source retrieved.
  - **WinForms/GDI+ (SGPro, unverified):** structurally weakest, because Microsoft documents GDI+ alpha blending as CPU-bound and the measured GDI+ figure (32 fps) in Microsoft's own test is **below** a 60 fps target before any application work. **However:** SGPro is a deep-sky sequencer, not a planetary capture tool — it does not need a 60 fps preview, so this is a mismatch of workload rather than a defect.
- **[INFERRED]** The WPF rendering tier is **a runtime property of the user's machine, not of the application**. A WPF capture app on a Tier 0 machine (headless VM, RDP session, a mini-PC with a weak iGPU and no WDDM driver, a Windows-on-ARM box with missing drivers) silently drops to software rendering. A Direct2D application has the same fallback risk in principle, but the developer can detect it and change strategy (e.g. a reduced-rate preview). This is an **observability and control** difference more than a raw-speed difference.
- **[INFERRED] High-DPI and multi-monitor:** WPF is device-independent-unit based and handles DPI scaling natively; WinForms and Swing historically required explicit per-monitor-v2 awareness and manual scaling work **[RECALLED]**. Pure Win32 + Direct2D requires the developer to implement per-monitor-v2 DPI handling, `WM_DPICHANGED` and swap-chain recreation by hand — real work, but fully under the developer's control. No URL retrieved for this paragraph; **treat as recalled**.

### Gaps
- No modern (Windows 11 / DirectX 12 era) Direct2D-vs-GDI+ benchmark found. The 415/120/32 fps figures are Windows 7-era.
- No source on Qt's or Swing's current default rendering backend on Windows.
- No source on any product's actual preview implementation.

---

## Part A7 — Developer velocity: pure Win32/C++ vs C#/WPF

### Takeaway
**This is the weakest-sourced section of the research and I want to be explicit about that.** I found **no credible, methodologically sound study** quantifying lines-of-code-per-feature for C++ versus C# in desktop application development. The widely circulated productivity multipliers in this space are folklore. What *is* well sourced is the memory-safety side of the trade-off (Part A9) and a few concrete capability differences.

### Cited Findings
- **Concrete capability difference, sourced:** N.I.N.A. 3.2 gives each plugin its own `AssemblyLoadContext`, so "no more dependency clashes between different plugins" — [N.I.N.A. release announcement](https://nighttime-imaging.eu/?p=1001). **This is a managed-runtime capability. The Win32/C++ equivalents are separate processes with IPC, or manual DLL version management — both substantially more work.** A thriving third-party plugin ecosystem is a direct, documented consequence of N.I.N.A.'s runtime choice.
- **Contribution pool, sourced indirectly:** N.I.N.A. has produced at least two public forks (PI.N.S., a Linux port; N.I.N.A. Polaris) — [awesome.ecosyste.ms](https://awesome.ecosyste.ms/projects/github.com%2Fdanwbr%2Fnina.polaris). The project is listed on good-first-issue aggregators — [goodfirstissue.org](https://www.goodfirstissue.org/isbeorn/nina). **[INFERRED]** This reflects both the open license and the accessibility of C#.
- **Memory-safety risk, sourced:** see Part A9 — Chromium found ~70% of serious security bugs were memory-safety problems ([Slashdot summary of the Chromium announcement](https://tech.slashdot.org/story/20/05/24/1349204/)), Microsoft reported ~70% of its security updates addressed memory-safety issues ([ZDNet coverage](https://cert.bournemouth.ac.uk/?p=8475)).
- **Build complexity, indirect:** ASTAP, a comparable solo-developer native project, is built with Free Pascal/Lazarus and ships for **Linux 32/64, Raspberry Pi 32/64, macOS 64, Windows 32/64** — [LinuxLinks](https://www.linuxlinks.com/astap-astrometric-stacking-program/). **[INFERRED]** A six-target native build matrix from one developer is achievable, but note ASTAP chose a cross-platform toolkit (Lazarus/LCL), not a platform-native UI API — exactly the choice pure-Win32 forecloses.

### Inferences
- **[INFERRED]** The honest velocity comparison for a **solo developer** is not language-level but library-level. C#/.NET ships a usable async model, a JSON serialiser, an HTTP and WebSocket stack, a collections library, a unit-test culture and a package manager (NuGet) in the box. A pure-Win32/C++20 project must select, vendor and maintain each of those — or write them. C++20 narrows the gap (coroutines, ranges, `std::format`, modules) but does not close it, and the absence of a universal package manager is a real recurring tax.
- **[INFERRED]** The honest counter-argument: for a *single-platform, single-developer, performance-first* application, a large part of the framework value is unrealised. There is no team to onboard, no cross-platform target, no dependency churn to absorb. Pure Win32 trades library breadth for **zero dependency risk and total determinism** — the entire behaviour of the program is in the author's source tree and the Windows SDK. For a 25-year enterprise-IT practitioner who has lived through framework deprecation cycles (.NET Framework → .NET Core → .NET, WinForms → WPF → WinUI → MAUI), that is not a sentimental preference; it is a defensible risk position.
- **[INFERRED]** **Testability** is where pure Win32/C++ is genuinely weakest. UI logic entangled with `WndProc` is hard to unit-test; WPF's MVVM pattern makes view-model logic trivially testable. This is a structural, not incidental, difference. It can be mitigated by strict separation of a testable core from a thin Win32 shell, but that discipline must be imposed by the author rather than by the framework.
- **[INFERRED]** **Hiring/contribution pool:** the astronomy-software volunteer pool skews toward Python and C#. A pure-Win32/C++20 codebase with no framework is a niche skill set even among C++ developers, most of whom have never written a raw message loop. For a project seeking contributions, this is a material cost; for a project that does not, it is irrelevant.

### Gaps
- **No LOC-per-feature study found.** I searched and found nothing methodologically defensible. **The report should not cite a productivity multiplier.** If one is wanted, it must be derived from the projects themselves (e.g. comparing N.I.N.A.'s public LOC against the comparison target's), and even then confounded by feature scope.
- No data on defect density by language in desktop astronomy software.
- No data on build times, CI complexity or release engineering effort for any of these products.

---

## Part A8 — Cross-platform reach as a strategic consequence

### Takeaway
Language choice determines reach, and in 2026 amateur astronomy has two live non-Windows constituencies: **Apple Silicon Macs** and **ARM Linux single-board computers** (Raspberry Pi, and the ASIAIR-class dedicated controllers). FireCapture (Java) and ASTAP (Lazarus) demonstrably reach both. N.I.N.A., despite being on .NET 8, is Windows-locked by WPF — and the response has been a community **fork** rather than a port. Pure Win32 is a hard, irreversible Windows lock-in.

### Cited Findings
- **FireCapture** ships official builds for **Windows x64, Linux (PPA), and aarch64** — [firecapture.de](https://firecapture.de)
- **ASTAP** runs on **Linux 32/64-bit, Raspberry Pi 32/64-bit, macOS 64-bit, Windows 32/64-bit**; written in Object Pascal, compiled with Free Pascal / Lazarus — [LinuxLinks](https://www.linuxlinks.com/astap-astrometric-stacking-program/). Packaged in Debian and Ubuntu, including **armhf** — [Debian astap](https://packages.debian.org/ko/bookworm/astap), [Ubuntu armhf](https://answers.launchpad.net/ubuntu/lunar/armhf/astap)
- **N.I.N.A.** is on **.NET 8** ([N.I.N.A. 3.0 release](https://nighttime-imaging.eu/2024/03/18/n-i-n-a-3-0/)) with WPF target framework `net8.0-windows7.0` ([NuGet](https://www.nuget.org/packages/NINA.CustomControlLibrary)) — i.e. the *runtime* is cross-platform but the *UI framework* is not. A separate Linux port exists as a **fork** (PI.N.S.), not as an official build — [awesome.ecosyste.ms](https://awesome.ecosyste.ms/projects/github.com%2Fisbeorn%2Fnina)
- **SharpCap** last-sourced requirement is **.NET Framework 4.8** (for 3.3) — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=18348). **.NET Framework is Windows-only by construction.** Whether 4.x has migrated is **[GAP]**.
- **Apple Silicon, 2026 status (KStars/Ekos as the bellwether):** KStars **3.8.1** released **2 February 2026**; as of **March 2026** a native, stable Apple Silicon build was **still outstanding**, with Mac users relying on community build scripts maintained by Rob Lancaster, and build-from-source instructions published by a community member in March 2026 — [borncity.com, 2026](https://borncity.com/news/kstars-community-treibt-astronomie-software-trotz-apple-huerden-voran/). **German-language secondary source; the only 2026-dated data point found. Verify before publication, and note it is 7 months stale relative to October 2026.**
- **Raspberry Pi / ARM Linux in practice:** **AstroArch** is a 64-bit OS for Raspberry Pi 3/4 packaging INDI drivers and tracking the latest INDI and KStars releases — [borncity.com](https://borncity.com/news/kstars-community-treibt-astronomie-software-trotz-apple-huerden-voran/). **StellarMate** is a commercial Raspberry Pi observatory controller "powered by Open Astronomy Instrumentation protocols (INDI), and Ekos," with companion apps on **Android and iOS** — [Astroshop StellarMate](https://www.astroshop.eu/controls/ikarus-technologies-stellarmate-telescope-astrophotography-controller/p,60907)
- Practitioner report: a complete imaging stack — "auto focusing, plate solving, camera control, filter wheel control, pointing, meridian flips, guiding and polar alignment via INDI, Ekos and KStars" — running on a Raspberry Pi for **under $200** — [IceInSpace forum](https://www.iceinspace.com.au/forum/showthread.php?p=1428034). **Practitioner report, undated.**
- For Linux desktop, the KStars project recommends the official **Flatpak on Flathub** — [borncity.com](https://borncity.com/news/kstars-community-treibt-astronomie-software-trotz-apple-huerden-voran/); KStars is also on Snap for Raspbian — [snapcraft.io](https://snapcraft.io/install/kstars/raspbian)

### Inferences
- **[INFERRED] The .NET Framework vs .NET distinction is the sharpest strategic fact in this section.** .NET Framework 4.8 is Windows-only, is in security-fix-only maintenance, and is a terminal branch. Modern .NET runs on Linux and macOS including ARM64. A product on .NET Framework has made a Windows-permanent choice *by inertia*; a product on .NET 8 has preserved runtime portability and spent it on WPF. These are different positions and should not be conflated. **Establishing which side of this line SharpCap sits on is the single highest-value unresolved fact in this research.**
- **[INFERRED]** N.I.N.A.'s case is the instructive one: **.NET alone does not buy portability if the UI layer is WPF.** Avalonia or MAUI would have; WPF did not. The appearance of a Linux fork rather than an official port is evidence the retrofit cost is high.
- **[INFERRED]** Pure Win32 + Direct2D is the most extreme position available: not merely Windows-only but *Win32-only*, foreclosing even Windows-on-ARM-via-other-toolkit paths and any future Microsoft UI stack migration. In exchange it gets the thinnest possible abstraction over the OS. This is a **coherent** choice for a planetary capture application specifically, because planetary capture is the one sub-discipline where the workflow is overwhelmingly "laptop beside the telescope" rather than "headless controller on the mount" — the Raspberry Pi constituency is concentrated in deep-sky automation (INDI/Ekos/StellarMate), not in 200 fps planetary runs, which are bandwidth-hostile to small-board hardware anyway.
- **[INFERRED]** The practical 2026 significance of Apple Silicon in amateur astronomy appears to be **real but underserved**: the flagship open-source stack still lacked a stable native build as of March 2026, which suggests the Mac constituency is large enough to sustain community build scripts but not large enough to have forced official support.

### Gaps
- Whether KStars shipped an official Apple Silicon build between March and October 2026 — not established. The report should either verify this or date the claim to March 2026.
- No market-share or install-base figures for Windows vs macOS vs Linux among amateur astronomers. I found none; the "practical importance" of Apple Silicon and ARM Linux can only be argued qualitatively from the evidence above.
- No source on whether ASIStudio ships ARM Linux builds.

---

## Part A9 — Memory safety and security posture

### Takeaway
The authoritative government guidance is unambiguous and names C and C++ explicitly as memory-unsafe, listing C# and Java among memory-safe alternatives. The empirical basis is the convergent ~70% figure from both Microsoft and Google. **For an application exposing a WebSocket server, this guidance applies directly and should be reported as fact, not opinion.** The important nuance is that the guidance addresses *software manufacturers shipping to customers*, and its stated deadlines have now passed.

### Cited Findings
- **CISA/NSA/FBI + UK, Canada, Australia, New Zealand** published joint guidance (**December 2023**) urging manufacturers to build roadmaps for moving away from C and C++, recommending memory-safe languages including **Rust, C#, Go, Java, Python, Swift** — [Security Boulevard coverage](https://securityboulevard.com/2023/12/cisa-to-developers-adopt-memory-safe-programming-languages), [TechRepublic](https://www.techrepublic.com/de/article/cisa-fbi-memory-safety-recommendations/)
- **The NSA's list of memory-safe languages does not include C++.** Its list comprises **Python, Java, C#, Go, Delphi/Object Pascal, Swift, Ruby, Rust, Ada** — [blog.kowatek.com summary](https://blog.kowatek.com/?p=37180). Coverage notes C and C++ "lack inherent memory protections that would prevent memory management issues."
- **Stated deadlines:** manufacturers were expected to have a roadmap for existing memory-unsafe products by **1 January 2026**; the guidance "does not apply to products that have an announced end-of-support date prior to January 1, 2030" — [TechRepublic](https://www.techrepublic.com/es/article/cisa-fbi-memory-safety-recommendations/). **As of 18 October 2026 the first deadline has passed; I found no source on compliance outcomes or any revision.**
- **Chromium empirical data:** a review of **912** high/critical-severity Chrome bugs fixed since 2015 found **~70% were memory-safety problems** — "Around 70% of our serious security bugs are memory safety problems" — [Chromium project, via Slashdot](https://tech.slashdot.org/story/20/05/24/1349204/), [TechRadar](https://www.techradar.com/news/nearly-all-google-chrome-security-bugs-involve-memory-flaws). Breakdown: **use-after-free 36%**, other memory-unsafety **32%** — [GrammaTech](https://www.grammatech.com/learn/memory-management-is-the-leading-cause-of-security-vulnerabilities-in-google-chrome/)
- **Microsoft empirical data:** at a **February 2019** security conference, Microsoft engineers stated that for the preceding **12 years, ~70% of all security updates for Microsoft products addressed memory-safety vulnerabilities** — [ZDNet coverage via CERT Bournemouth](https://cert.bournemouth.ac.uk/?p=8475)
- **C++ community response:** the C++ community is pursuing **profiles**, which provide safety guarantees at compile time; "not everyone agrees C++ is up to the challenge" — [KDAB: Memory-Safety Roadmap for Secure Programming](https://kdab.com/software-technologies/rust/memory-safety-roadmap-for-secure-programming)
- **Caveat on the data:** both ~70% figures are from **2019–2020**. No newer figures were found in this research.

### Inferences
- **[INFERRED]** The guidance's force is proportional to attack surface. A WebSocket telemetry server **is** remotely reachable attack surface: it parses attacker-influenceable framing (length fields, masking, fragmentation, continuation frames, UTF-8 validation) on a network socket. This is the exact shape of bug class the 70% figure describes. A hand-rolled C++ WebSocket frame parser without fuzzing, sanitizers and bounds-checked buffer types is the single highest-risk component in a native capture application.
- **[INFERRED]** The practical mitigations a C++20 project can point to are real but must be *demonstrated*, not asserted: `std::span`/`std::string_view` with bounds-checked access, `gsl::not_null`, AddressSanitizer and UndefinedBehaviorSanitizer in CI, a continuous fuzzing harness over the frame parser, `/guard:cf` and `/sdl` compiler flags, and binding the listener to loopback by default. **[RECALLED]** — no URL retrieved for this list.
- **[INFERRED]** The counter-consideration the report should give fairly: a managed runtime eliminates the *spatial and temporal memory-safety* bug classes but does not eliminate logic flaws, authentication gaps, deserialisation vulnerabilities, or denial-of-service. A C# WebSocket server with no authentication is less safe in practice than a carefully fuzzed C++ one with a token. The 70% figure says memory safety is the *largest single* class, not the only one.
- **[INFERRED]** Scope mismatch worth flagging: the CISA guidance targets commercial software manufacturers shipping to enterprise and government customers. A hobbyist astrophotography application binding a telemetry socket to the local network is not the threat model the guidance was written for. It remains the correct *default engineering posture*, and it is the standard a reviewer with an AI-governance and enterprise-IT background will apply — but the report should state the scope accurately rather than implying regulatory obligation.

### Gaps
- **I could not read the primary CISA/NSA document.** All findings above are from secondary coverage. **The report writer must verify the exact title, date, wording and current status directly at cisa.gov before publication.** The document is believed to be *"The Case for Memory Safe Roadmaps"* (December 2023) **[RECALLED]**.
- No post-2020 memory-safety vulnerability statistics found.
- No source on compliance with, or revision of, the January 2026 roadmap deadline.
- Current status of C++ **profiles** in the standard (C++26) — not established.

---

## Part B1 — ASCOM and ASCOM Alpaca

### Takeaway
Classic ASCOM is a **Windows COM** standard; **Alpaca** is its HTTP/REST/JSON successor with **no Windows dependency**, UDP-based discovery, and a bridging server for legacy COM drivers. The current Platform is **7.1** (3 November 2025) with **Update 2** (10 February 2026), and the Alpaca API Reference is at **version 12, February 2026** — i.e. both are actively maintained as of early 2026.

### Cited Findings
- **Platform 7.1 released 3 November 2025**; **Platform 7.1 Update 2 released 10 February 2026** — [ScopeTrader](https://scopetrader.com/ascom-platform-7.1-released/), [ScopeTrader](https://scopetrader.com/ascom-platform-7.1-update-2-released/). **Both from a third-party astronomy news site, not from ASCOM directly — verify at ascom-standards.org.** Platform 7.0 also had an Update 2 — [ScopeTrader](https://scopetrader.com/ascom-platform-7.0-update-2-now-available/)
- 7.1 coverage mentions "new .NET 6 compatibility features and improved tools for creating compliant drivers across Windows environments" — [ScopeTrader](https://scopetrader.com/ascom-platform-7.1-released/)
- **Alpaca is platform-independent:** "Alpaca uses TCP/IP, HTTP, REST and JSON and has no Windows dependency, so Linux and macOS clients can use it" — [ASCOM Alpaca Introduction (PDF), Peter Simpson & Bob Denny](https://download.ascom-standards.org/docs/AlpacaIntroduction.pdf)
- **Discovery protocol:** clients broadcast a UDP discovery message over IPv4 to default port **32227**; IPv6 clients use link-local multicast address **ff12::a1:9aca**. Devices reply with a minimal JSON object **`{AlpacaPort: n}`**, which the client combines with the sender's IP — [ASCOM Alpaca API Reference](https://ascom-standards.org/AlpacaDeveloper/ASCOMAlpacaAPIReference.html), [Alpyca discovery docs](https://www.ascom-standards.org/alpyca/alpaca.discovery.html)
- After discovery, clients use the **Management API** to enumerate devices. Each entry must include "the device's name, its ASCOM device type, the device number that must be used to communicate with this particular ASCOM Device and a globally unique id" — the GUID supports **re-discovery when IP addresses change** — [Alpyca management docs](https://ascom-standards.org/alpyca/alpaca.management.html)
- **Current spec version: ASCOM Alpaca API Reference version 12, dated February 2026**, with discovery documented in section 5 — [ASCOM Alpaca API Reference](https://ascom-standards.org/AlpacaDeveloper/ASCOMAlpacaAPIReference.html)
- **COM bridging:** "ASCOM Remote Server — Enables Alpaca clients to discover and use COM drivers" — [ASCOM Documentation Center](https://ascom-standards.org/Documentation/Index.htm)
- **Client libraries:** Python **Alpyca 3.1.2/3.1.3** (`search_ipv4` discovers Alpaca device servers on the IPv4 LAN/VLAN) — [Alpyca docs](https://ascom-standards.org/alpyca/alpaca.device.html); .NET `AlpacaDiscovery` component — [ASCOM .NET library help](https://ascom-standards.org/help/html/Methods_T_ASCOM_Utilities_AlpacaDiscovery.htm); **AlpycaDevice SDK 1.0.3** for writing Alpaca drivers in Python — [AlpycaDevice docs](https://ascom-standards.org/alpycadevice/introduction.html)
- **Alpyca 3.0.0** on PyPI expanded to support the additions in ASCOM Platform 7 — [PyPI alpyca](https://pypi.org/project/alpyca); **ASCOM.Alpaca.Components** NuGet latest **2.0.9**, updated **11 February 2025**, supporting the Platform 7 interface updates — [NuGet](https://feed.nuget.org/packages/ASCOM.Alpaca.Components/2.0.1); **ASCOM.Common.Components 2.0.7** also published — [NuGet](https://www-1.nuget.org/packages/ASCOM.Common.Components)

### Inferences
- **[INFERRED]** The architectural consequence for the comparison: a **COM** interface is natural and nearly free from C++ (COM *is* a C++-era binary interface) and from .NET (COM interop is built in). It is awkward from Java. **Alpaca inverts this** — an HTTP/JSON client is trivial in every language, which is precisely why Alpaca exists. A Java or cross-platform capture application is strictly better served by Alpaca; a Win32/C++ application can consume either, with COM being the lower-latency local path and Alpaca the network path.
- **[INFERRED]** Alpaca does not replace ASCOM's *interface definitions* — it re-expresses the same device interfaces over HTTP. Platform 7's interface updates propagate to both. The two are a single standard with two transports, not competing standards.
- **[INFERRED] Adoption state in 2026:** actively maintained (Platform 7.1 Update 2 in February 2026, API Reference v12 in February 2026, client libraries updated for Platform 7). I found **no adoption statistics** — no count of Alpaca-native drivers versus COM drivers, no survey of which applications are Alpaca clients. The existence and continued promotion of **ASCOM Remote Server** as a COM→Alpaca bridge is **[INFERRED]** evidence that COM drivers remain the majority of the installed base.

### Gaps
- **No primary ASCOM source was readable** (ascom-standards.org unreachable from this environment). Platform version and dates come from ScopeTrader, a third-party news site. **Verify directly.**
- No quantitative adoption data for Alpaca versus classic ASCOM.
- Which of the six products in scope support Alpaca natively — not established for any.

---

## Part B2 — INDI and INDIGO

### Takeaway
**INDI** is a POSIX, LGPL, process-per-driver architecture mediated by `indiserver` over stdin/stdout, with socket-based clients — the de facto standard on Linux, macOS and Raspberry Pi. **INDIGO** is a successor project by the same lineage that replaces the client-server model with a **multi-tier, agent-based architecture over a software bus**. Neither is Windows-native in practice.

### Cited Findings

**INDI:**
- "An open-source POSIX implementation of the Instrument-Neutral-Device-Interface protocol." Three main parts: the **server (`indiserver`)**, the **core drivers**, and **cross-platform client libraries in POSIX and Qt5 flavors**, embeddable in third-party applications — [GitHub INDI README](https://github.com/irydeo/indi)
- **Process isolation:** "`indiserver` starts each driver as its own process and passes it protocol traffic over stdin/stdout." Clients connect over sockets; clients **can also connect to drivers directly, bypassing the server** — [GitHub INDI README](https://github.com/irydeo/indi)
- **Licence: LGPL**; includes header-only helpers for JSON, HTTP and INI — [indilib/docs](https://www.github.com/indilib/docs)
- **Device categories:** mounts, cameras (CCD, CMOS, DSLR, webcam), focusers, domes, filter wheels, GPS, weather stations. Core drivers ship with the library; third-party drivers live in a separate **indi-3rdparty** repository — [OpenBSD package page](https://www.openbsdhandbook.com/packages/current/devel/indi/), [OpenBSD release page](https://www.openbsdhandbook.com/packages/release/devel/indi/)
- **Platforms:** builds on **Linux and BSD** with CMake — [indilib/docs](https://www.github.com/indilib/docs). Packaged for OpenBSD, Debian, Docker — [Docker Hub](https://hub.docker.com/r/jochym/indi-bionic)
- A **.NET client binding** exists (`INDISharp.INDI` on NuGet) — [NuGet](https://packages.nuget.org/packages/INDISharp.INDI). **[INFERRED]** Community-maintained, not official.

**INDIGO:**
- "A system of standards and frameworks for multiplatform and distributed astronomy software development" — [CloudMakers INDIGO Suite docs](https://cloudmakers.eu/doc/indigosuite_4.6/astroimager.html)
- Explicitly positioned as "a next generation of INDI, based on layered architecture and software bus" — [CloudMakers](https://cloudmakers.eu/doc/indigosuite_4.4/astroimager.html)
- Architectural change: "switched to multi-tier agent based architecture allowing even higher performance and reliability," moving away from the traditional client-server model — [CloudMakers](https://cloudmakers.eu/doc/indigosuite_4.4/astroimager.html)
- Design philosophy: "our suite is not a single monolithic application" — loosely coupled standalone applications — [CloudMakers](https://cloudmakers.eu/doc/indigosuite_4.6/astrotelescope.html)
- **INDIGO Suite** (macOS/iOS) components: INDIGO A1 / D1 all-in-one (AstroImager or AstroDSLR + AstroGuider + AstroTelescope with an embedded server), plus INDIGO Server, Control Panel, Script Editor, Dashboard, FITS Preview, Astrometry — [CloudMakers](https://cloudmakers.eu/doc/indigosuite_4.6/astroimager.html)
- Version pairing: framework **2.0.148** with Suite 4.6; framework **2.0.176** with Suite 4.4 — [CloudMakers docs](https://cloudmakers.eu/doc/indigosuite_4.6/astroimager.html). **Note: these version numbers appear inconsistent (a higher framework version paired with a lower suite version); treat with suspicion.**

**Which products use INDI:**
- **KStars/Ekos** — the reference client stack — [borncity.com](https://borncity.com/news/kstars-community-treibt-astronomie-software-trotz-apple-huerden-voran/)
- **StellarMate** — "powered by Open Astronomy Instrumentation protocols (INDI), and Ekos, a cross-platform astrophotography suite for Windows, MacOS, and Linux" — [Astroshop](https://www.astroshop.eu/controls/ikarus-technologies-stellarmate-telescope-astrophotography-controller/p,60907)
- **AstroArch** — Raspberry Pi OS packaging INDI drivers, tracking latest INDI and KStars — [borncity.com](https://borncity.com/news/kstars-community-treibt-astronomie-software-trotz-apple-huerden-voran/)
- **[GAP]** None of the six products in scope is documented as an INDI client in anything I retrieved.

### Inferences
- **[INFERRED]** INDI's **process-per-driver** model is a genuine architectural contrast with ASCOM's in-process COM model: a crashing INDI driver takes down one process, not the application. The cost is IPC on every property update. For a planetary capture application that needs a camera frame path, INDI's stdin/stdout driver channel is unsuitable for the data path and is used for control; the ASCOM/vendor-SDK model is the one used for high-rate frame delivery.
- **[INFERRED]** The practical split in 2026: **ASCOM/Alpaca on Windows, INDI on Linux/macOS/Pi, INDIGO on Apple platforms**. A Windows-only capture application has no reason to implement INDI; a cross-platform one has no choice.

### Gaps
- INDIGO documentation came **only from CloudMakers**, a vendor of INDIGO-based macOS/iOS applications — not from the INDIGO project itself (`indigo-astronomy.org`). The architectural description may be vendor-slanted.
- No official INDIGO licence, platform matrix, or adoption data retrieved.
- No source establishing whether INDI/INDIGO are used by any of the six in-scope products.

---

## Part B3 — The SER video format

### Takeaway
SER is a simple, uncompressed container: a **178-byte header**, raw frame data, and an **optional timestamp trailer carrying one UT timestamp per frame**. **It carries no version field** — version is inferred from the `ColorID` value. The authoritative specification is a PDF by **Heiko Wilkens and Grischa Hahn**. **I was unable to read the specification itself** (host unreachable), so byte-level details below are from secondary sources and must be verified against the PDF.

### Cited Findings
- **Structure:** three parts — a **178-byte header** containing image and observation information, the raw pixel data, and an **optional trailer holding a date for each image in the sequence** — [Siril documentation: SER file format](https://siril-localized-doc.readthedocs.io/fr/stable/_sources/file-formats/SER.rst.txt)
- **Timestamps:** the format stores "time stamps (UT) for every frame" — [Grischa Hahn, SER file format](https://grischa-hahn.homepage.t-online.de/astro/ser/index.htm)
- **Version identification — there is no version tag.** `ColorID` **[0..19]** ⇒ **version 2**; `ColorID` **[100..101]** ⇒ **version 3** — [FFmpeg trac ticket 6821](https://trac.ffmpeg.org/ticket/6821)
- **v3 (2014) added RGB handling:** "SER v3 supports RGB/BGR 8/16-bit data" — [FFmpeg trac ticket 6821](https://trac.ffmpeg.org/ticket/6821)
- **Authoritative documents:** Grischa Hahn's page hosts a **version 2 description by Heiko Wilkens** and a **version 3 description by Wilkens and Hahn**, plus a **sample Delphi/Pascal implementation of v3** — [Grischa Hahn, SER file format](https://grischa-hahn.homepage.t-online.de/astro/ser/index.htm). The v3 PDF (referred to as **`SER_Doc_V3b.pdf`**) is **mirrored on free-astro** — [Siril documentation](https://siril-localized-doc.readthedocs.io/fr/stable/_sources/file-formats/SER.rst.txt)
- The FFmpeg discussion states explicitly that "the version 3 description should be the reference" — [FFmpeg trac ticket 6821](https://trac.ffmpeg.org/ticket/6821)
- **Applications:** **SER Player** by **Chris Garry** (`cgarry/ser-player`) is a **C++/Qt** SER video player; its README links the v3b PDF as the specification — [codegraph: ser-player README](https://codegraph.jelmer.uk/ser-player/1.7.3-3/README.md). Packaged in Debian-family distributions (ser-player 1.7.3-3) — [PureOS](https://software.pureos.net/package/bin/pureos/dawn/ser-player), [Trisquel](https://packages.trisquel.info/ja/nabia/ser-player)
- **Siril** reads SER — [Siril documentation](https://siril-localized-doc.readthedocs.io/fr/stable/_sources/file-formats/SER.rst.txt)
- **FFmpeg does not natively support SER** — ticket 6821 is an open feature request, opened approximately 2017 and still shown as open — [FFmpeg trac ticket 6821](https://trac.ffmpeg.org/ticket/6821)

### Inferences
- **[INFERRED]** The practitioner evidence (Part A3) that capture applications write SER at 185+ fps directly to disk implies SER's appeal is precisely its **lack of structure**: a fixed header, then `fwrite` of raw frames, then a trailer. There is no muxer, no index, no compression, no container state. This is why a capture application can be I/O-bound rather than CPU-bound when writing it, and it is the reason SER rather than AVI became the planetary standard.
- **[INFERRED]** The **absence of a version field** is a real interoperability hazard: a reader must sniff `ColorID` ranges to know which layout it is parsing. Any capture application writing SER must get the `ColorID` right or downstream tools will misinterpret the data.
- **[INFERRED]** The **optional** trailer matters for the comparison: a capture application that omits per-frame timestamps produces a file that is valid SER but unusable for WinJUPOS derotation, which needs accurate per-frame mid-times. Writing the trailer is therefore not optional in practice for planetary work.

### Gaps
- **I could not read the SER specification.** The 178-byte header's field layout, the exact timestamp encoding (believed to be a **.NET-style 64-bit tick count at 100 ns resolution since 0001-01-01** — **[RECALLED]**, unverified), the `LittleEndian` flag semantics, and the complete `ColorID` enumeration are all unverified. **The report writer must obtain `SER_Doc_V3b.pdf` directly.**
- **[GAP] WinJUPOS compatibility requirements were not established by any source.** No source retrieved states what WinJUPOS specifically requires of a SER file (timestamp presence, format, filename convention, or ColorID support). This was a key question and I could not answer it.
- No source enumerating which of the six in-scope products read and/or write SER. **[RECALLED]** FireCapture, SharpCap and ASICap all write SER; this is near-universal practitioner knowledge but I retrieved no citation.

---

## Part B4 — FITS and XISF

### Takeaway
**FITS 4.0** (approved 22 July 2016 by the IAU FITS Working Group; language-edited PDF 13 August 2018) is current. **XISF 1.0** (PixInsight, Juan Conejero) is the current XISF specification and no later version was found.

### Cited Findings
- **FITS 4.0** "was formally approved by the IAU FITS Working Group on 22 July 2016," with a language-edited PDF following on **13 August 2018** — [Wikipedia: FITS](https://en.wikipedia.org/wiki/FITS), [NASA GSFC FITS Standard](https://fits.gsfc.nasa.gov/fits_standard.html)
- Relative to FITS 3, version 4.0 "officially recognizes" several previously de facto conventions, including **time world coordinates** and **compression of images and tables** — [Wikipedia: FITS](https://en.wikipedia.org/wiki/FITS)
- **XISF 1.0** specification authored by **Juan Conejero, PixInsight Development Team**; "released officially after three years of working drafts" — [XISF Version 1.0 Specification](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html), [pixinsight.com/xisf](https://pixinsight.com/xisf/)
- No XISF version after 1.0 was found in the sources searched — [XISF spec](https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html)
- **ASTAP** ships an **ASIFitsView**-equivalent role and is a FITS-centric tool **[INFERRED from]** — [LinuxLinks: ASTAP](https://www.linuxlinks.com/astap-astrometric-stacking-program/); **ZWO ASIStudio** includes a module named **ASIFitsView** — [ZWO](https://us.zwoastro.com/blogs/product-information/what-is-asistudio-and-what-can-it-bring-us)

### Inferences
- **[INFERRED]** The division of labour is clean and worth stating in the report: **FITS for single long-exposure deep-sky frames (with rich metadata headers), SER for high-rate planetary video, XISF as a PixInsight-ecosystem archival/interchange format.** A planetary capture application's obligations are SER-first with FITS as a secondary single-frame output; a deep-sky sequencer's are FITS-first. XISF is essentially a post-processing format and is not a capture-application obligation.
- **[INFERRED]** FITS 4.0's formal adoption of **time world coordinates** is directly relevant to capture applications that need to record precise per-frame timing in FITS — it gives a standard place to put what SER puts in its trailer.

### Gaps
- Which of the six products write FITS, and at what FITS version/conformance level — not established for any.
- Whether any capture application writes XISF — not established. **[INFERRED]** unlikely; XISF is written by PixInsight and by a small number of acquisition tools.
- No source confirming whether an XISF 1.1 or 2.0 exists as of 2026.

---

## Part B5 — The planetary post-processing chain

### Takeaway
The canonical planetary chain is **capture → AutoStakkert! (stack) → RegiStax or equivalent (wavelet sharpen) → WinJUPOS (derotation / measurement)**, with PlanetarySystemStacker as an open-source alternative stacker and ASTAP serving deep-sky rather than planetary needs. **The implication for a capture application is sharply bounded responsibility:** deliver a SER file with accurate per-frame timestamps and honest metadata, and stop. **Evidence on this section was thinner than expected** — I found no primary documentation for AutoStakkert! or PlanetarySystemStacker.

### Cited Findings
- **Role of the stacker:** "Programs such as AutoStakkert! or RegiStax" work by "discarding the frames most affected by atmospheric movement" and stacking the remainder into a master frame — [BBC Sky at Night Magazine](https://skyatnightmagazine.com/astrophotography/astrophoto-tips/use-winjupos-derotate-planetary-images)
- **Why derotation is needed:** long captures smear on a rotating planet; "in recent years the freeware program WinJupos has been enhanced to enable you to correct for planetary rotation" — [BBC Sky at Night Magazine](https://skyatnightmagazine.com/astrophotography/astrophoto-tips/use-winjupos-derotate-planetary-images)
- **WinJUPOS derotation capability and cost:** it "has a sophisticated derotate feature that can correct each frame in a video," but it is "complicated and slow" — [BBC Sky at Night Magazine](https://skyatnightmagazine.com/astrophotography/astrophoto-tips/use-winjupos-derotate-planetary-images)
- **Community-reported workflow:** AutoStakkert **4** for stacking; RegiStax or PixInsight for sharpening; **FireCapture** for recording; **WinJUPOS 12** for derotation — [AstroBin forum](https://app.astrobin.com/forum/topic/118808/processing/best-stacking-software-for-planetary-images). **Practitioner report.**
- Performance note: "AutoStakkert uses a lot less memory and is faster" than stacking a deep-sky target in PixInsight or DSS — [AstroBin forum](https://app.astrobin.com/forum/topic/118808/processing/best-stacking-software-for-planetary-images). **Practitioner report.**
- **Handoff pattern:** a standalone post-processing tool is described as taking "AutoStakkert or AviStack output" and passing selected results "to WinJUPOS derotation or animation" — [AstroBin forum](https://app.astrobin.com/forum/topic/24325/processing/new-software-for-automatic-post-processing-of-planetary-solar-or-lunar-image-stacks)
- **An open-source entrant:** **LuckyStackWorker** is an open-source planetary post-processing application under active beta — [AstroBin forum](https://app.astrobin.com/forum/topic/61846/new-version-of-the-open-source-planetary-post-processing-app-luckystackworker-beta-testers-wanted)
- **ASTAP's actual role:** "Astrometric STAcking Program" — a plate solver and deep-sky stacker, written in Object Pascal / Lazarus, with a solver that is "command line compatible with PlateSolve2" — [LinuxLinks: ASTAP](https://www.linuxlinks.com/astap-astrometric-stacking-program/)

### Inferences
- **[INFERRED] Where a capture application's responsibility ends.** The post-processing chain is mature, specialised and free. AutoStakkert does alignment-point selection, quality ranking and stacking; RegiStax does wavelets; WinJUPOS does ephemeris-aware derotation and positional measurement. **A capture application that tries to do any of these is competing with purpose-built tools that are better at them.** The capture application's contract is narrow and strict:
  1. A **SER file with a correct header and a complete, accurate per-frame UT timestamp trailer** — this is what WinJUPOS derotation consumes and the one thing no downstream tool can reconstruct.
  2. **No lossy transformation.** No debayer-and-rewrite, no compression, no gamma. Raw sensor data, so the stacker can make its own decisions.
  3. **Honest metadata** — gain, exposure, ROI, binning, sensor temperature, and crucially the **exact frame count and dropped-frame count**, so the user knows whether the capture is trustworthy.
  4. **Sustained, lossless throughput** — which returns the whole question to Part A3.
- **[INFERRED]** The capture application's *preview* and *sharpness/quality metric* features are therefore not post-processing encroachment but **real-time decision support**: they exist so the observer can judge focus, collimation and seeing *during* the run, when the information is actionable. That is the legitimate boundary.
- **[INFERRED]** ASTAP is **mis-grouped** if placed in the planetary chain. It belongs in the deep-sky/plate-solving category (Part B6). The report should not list it alongside AutoStakkert and WinJUPOS.

### Gaps
- **No primary documentation for AutoStakkert!** retrieved — no source on its exact input format requirements, supported SER variants, or output format.
- **No information on PlanetarySystemStacker at all.** No source retrieved.
- **[GAP]** No source confirms whether AutoStakkert's output feeds WinJUPOS directly or requires an intermediate step, and no source describes WinJUPOS's input requirements. This was a specific question in the brief and I could not answer it.
- RegiStax's current version and maintenance status — not established.

---

## Part B6 — Plate solving

### Takeaway
**ASTAP** is the modern default — fast, offline, blind-solve capable, GPL, cross-platform including Raspberry Pi — and critically it is **command-line compatible with PlateSolve2**, which is why it drops into existing integrations. **PlateSolve2 cannot blind-solve.** Local astrometry.net is the classic offline fallback but can be very slow on weak hardware. The near-universal integration method in this category is **child-process invocation with command-line arguments**, not an API.

### Cited Findings
- **ASTAP:** "fast and reliable," able to do **blind solves without an internet connection**, but "requires precise parameter setup"; described elsewhere as "the most modern option" which "requires approximate coordinates and can also be used for blind solving" — [N.I.N.A. documentation: Plate Solving](https://nighttime-imaging.eu/docs/master/site/advanced/platesolving/), [N.I.N.A. docs](https://nighttime-imaging.eu/docs/develop/site/advanced/platesolving.html)
- **ASTAP licensing and platforms:** "free and open source software," licensed **GPL** (version unspecified in the AUR metadata); written in **Object Pascal**, compiled with **Free Pascal / Lazarus**; runs on **Linux 32/64, Raspberry Pi 32/64, macOS 64, Windows 32/64**; upstream at **hnsky.org** — [LinuxLinks: ASTAP](https://www.linuxlinks.com/astap-astrometric-stacking-program/), [AUR astap-bin](https://aur.archlinux.org/packages/astap-bin), [Debian astap](https://packages.debian.org/ko/bookworm/astap)
- **The integration-critical fact:** ASTAP's solver is **"command line compatible with PlateSolve2"** — [LinuxLinks: ASTAP](https://www.linuxlinks.com/astap-astrometric-stacking-program/)
- **Astrometry.net online:** solving runs on the project's servers; requires an **account and API key**. "Most installer bundles for Windows deliver rather outdated versions" — [N.I.N.A. documentation](https://nighttime-imaging.eu/docs/master/site/advanced/platesolving/)
- **Astrometry.net local:** **All Sky Plate Solver** is "basically a wrapper for the local astrometry.net client"; "setup is much simpler, and it works offline" — [N.I.N.A. documentation](https://nighttime-imaging.eu/docs/master/site/advanced/platesolving/). Practitioner report: "on my atom based laptop it might take up to 8 minutes to solve some images," with the suggested workaround being to run the solver on a faster networked machine — [Stargazers Lounge](https://stargazerslounge.com/topic/304830-astrometrynet-local-sgp-doesnt-solve). **Practitioner report.**
- **PlateSolve2:** "makes fast near solving and requires approximate coordinates"; a forum user confirmed **it is not a blind solver** — [N.I.N.A. documentation](https://nighttime-imaging.eu/docs/master/site/advanced/platesolving/), [SGPro forum](https://forum.sequencegeneratorpro.com/t/platesolve2-seems-to-work-very-well/1906)
- **PlateSolve3:** PlaneWave's standalone solver; "handles longer focal lengths and small FOVs, and solves quickly with few stars" — [N.I.N.A. documentation](https://nighttime-imaging.eu/docs/master/site/advanced/platesolving/). A user reported "PlateSolve is usually somewhat better at blind solving fields than the web Nova tool" — [SGPro forum](https://forum.sequencegeneratorpro.com/t/plate-solving-step-by-step-anyone/2532). **Practitioner report; note this appears to conflict with the statement that PS2 cannot blind-solve — the user may be referring to PS3.**
- **Known defect:** "PS2 and PS3 could have problems with solving on computers with non US/UK numeric format, also can have difficult times when short focal length lens is used" — [N.I.N.A. documentation](https://nighttime-imaging.eu/docs/master/site/tabs/options/platesolving/). *A locale-dependent decimal-separator bug — a classic symptom of culture-sensitive number parsing.*
- N.I.N.A. exposes solver selection as a configuration option with multiple backends — [N.I.N.A. plate solving options](https://nighttime-imaging.eu/docs/develop/site/tabs/options/platesolving.html)

### Inferences
- **[INFERRED] Typical integration method:** the applications treat solvers as **external executables invoked with command-line arguments**, writing a FITS/image file to a temp path and parsing the solver's output. This is directly evidenced by ASTAP advertising **command-line compatibility with PlateSolve2** as its principal integration selling point, and by All Sky Plate Solver existing as "a wrapper for the local astrometry.net client." **Consequence for the comparison: plate-solver integration is language-agnostic.** Process spawn and text parsing are equally easy in C++, C#, Java and Python. This is *not* an area where the implementation language differentiates the products.
- **[INFERRED]** ASTAP's GPL licence is a constraint worth naming: linking ASTAP *as a library* into a closed-source or PolyForm-Noncommercial-licensed application would raise GPL obligations. **Invoking it as a separate process does not** — which is a further reason the child-process integration pattern dominates. This is a licensing fact with direct architectural consequence.
- **[INFERRED]** Plate solving is largely irrelevant to **planetary** capture (the targets are bright, known, and the FOV is tiny). It is central to deep-sky sequencers. This asymmetry explains why SGPro and N.I.N.A. have deep plate-solving integration and planetary tools largely do not — and a comparison should not penalise a planetary capture application for lacking it.

### Gaps
- Exact GPL version for ASTAP — not established (AUR lists bare "GPL").
- PlateSolve2 and PlateSolve3 licence terms — not established. **[RECALLED]** both are freeware from PlaneWave/Dave Rowe; unverified.
- Current astrometry.net version and whether a modern Windows build exists — not established.

---

## Part B7 — PHD2 and guiding interoperability

### Takeaway
PHD2 exposes a **line-delimited JSON event stream and command interface over TCP port 4400** (incrementing for additional instances). It is a trivially consumable protocol from any language — newline-delimited JSON over a socket — so like plate solving, it is not a point of language differentiation.

### Cited Findings
- "Clients connect to PHD2 on **TCP port 4400**." If several instances run, later instances use **successive port numbers (4401, 4402, …)** — [PHD2 wiki: EventMonitoring](https://github-wiki-see.page/m/OpenPHDGuiding/phd2/wiki/EventMonitoring)
- On connecting, a client receives initial messages, then **notifications as guiding events occur**. "Event Notification messages are formatted as JSON objects. Each message is a **single line of text terminated by CR LF**" — [PHD2 wiki: EventMonitoring](https://github-wiki-see.page/m/OpenPHDGuiding/phd2/wiki/EventMonitoring)
- **Common fields:** "Each message has **Event, Timestamp, Host, and Inst** attributes" — [PHD2 wiki: EventMonitoring](https://github-wiki-see.page/m/OpenPHDGuiding/phd2/wiki/EventMonitoring)
- **Documented use cases:** "graph guide data in real time, alert on events such as guiding being lost, or **control PHD2 from an imaging application**" — [PHD2 wiki: EventMonitoring](https://github-wiki-see.page/m/OpenPHDGuiding/phd2/wiki/EventMonitoring)
- A **separate repository of ready-to-use client code** is maintained by the project — [PHD2 wiki: EventMonitoring](https://github-wiki-see.page/m/OpenPHDGuiding/phd2/wiki/EventMonitoring)
- The initial version message reports an **event protocol version**, given as **1** at the time of the wiki page — [PHD2 wiki: EventMonitoring](https://github-wiki-see.page/m/OpenPHDGuiding/phd2/wiki/EventMonitoring)

### Inferences
- **[INFERRED]** Newline-delimited JSON over a raw TCP socket is the lowest-friction integration protocol in this entire ecosystem — lower than COM (needs a Windows type library), lower than the camera SDKs (need P/Invoke/JNI declarations), lower than Alpaca (needs an HTTP client). Any of the six languages in scope consumes it in a few dozen lines.
- **[INFERRED]** The architectural point worth making in the report: PHD2 and Alpaca both chose **text-over-socket** rather than a binary in-process ABI, and the result is universal interoperability. This is the strongest available argument that **interop boundaries in this ecosystem are deliberately language-neutral** — the ecosystem has standardised on protocols that no implementation language advantages.
- **[INFERRED]** Guiding is irrelevant to planetary capture (sub-second exposures, no guiding), so PHD2 integration is a deep-sky-sequencer feature. Again, not a fair axis on which to compare a planetary capture application.

### Gaps
- The PHD2 **RPC method list** (the command surface, as distinct from the event surface) was not retrieved. The brief asked for "the server/API surface"; I have the event half but not the command half. **[RECALLED]** The RPC layer is JSON-RPC-style with methods such as `guide`, `dither`, `set_paused`, `get_app_state` — unverified.
- Whether the event protocol version is still 1 in 2026 — not established.
- PHD2's licence — not established in this research. **[RECALLED]** BSD-3-Clause; unverified.

---

## Part B8 — The licensing landscape

### Takeaway
The category spans the full licensing spectrum: **MPL-2.0** (N.I.N.A.), **LGPL** (INDI), **GPL** (ASTAP), **freeware closed-source** (FireCapture, ASIStudio), **paid subscription** (SharpCap Pro, SGPro), and **source-available noncommercial** (PolyForm Noncommercial 1.0.0). PolyForm Noncommercial grants broad rights — including modification, distribution and a patent licence — **but only for noncommercial purposes**, with hobby projects and personal study explicitly named as permitted. **It is not an open-source licence** (it fails the OSI's no-discrimination-against-fields-of-endeavour criterion), which is the decisive fact for a project seeking contributions.

### Cited Findings

**PolyForm Noncommercial 1.0.0:**
- **Core grant:** a broad copyright licence to do what would otherwise infringe, **but only for permitted purposes**; "any noncommercial purpose is a permitted purpose" — [SPDX: PolyForm-Noncommercial-1.0.0](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)
- **Explicitly named permitted uses:** "personal uses such as research, experiment, and testing for the benefit of public knowledge, **personal study, private entertainment, hobby projects, amateur pursuits**, or religious observance"; also **charities, schools, public research bodies, and government institutions** — [SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)
- **Distribution is a separate grant**, and it **covers copies with changes and new works** — [SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)
- **Includes a patent licence** — [SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)
- **Fair use is not limited** by the licence — [SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)
- **No sublicensing or transfer** of the licences — [SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)
- **Date discrepancy, flagged:** SPDX states "This license was released on **July 9, 2019**"; the PolyForm Project's own 1.0.0 announcement page is dated **May 2020** — [SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html) vs [PolyForm Project](https://polyformproject.org/2020/05/24/version-1). **Sources conflict. Resolve before publication.**
- The PolyForm Project announced version 1.0.0 of the licence family — [polyformproject.org](https://polyformproject.org/?p=70)
- Real-world adopters include hyppo (neurodata) — [hyppo license page](https://hyppo.neurodata.io/development/license)

**Other licences in the category:**
- **N.I.N.A.: Mozilla Public License 2.0** — [N.I.N.A. docs](https://nighttime-imaging.eu/docs/master/site), [GitHub](https://github.com/isbeorn/nina)
- **INDI: LGPL** — [indilib/docs](https://www.github.com/indilib/docs)
- **ASTAP: GPL** (version unspecified), free and open source — [LinuxLinks](https://www.linuxlinks.com/astap-astrometric-stacking-program/), [AUR](https://aur.archlinux.org/packages/astap-bin)
- **SharpCap:** free tier plus **SharpCap Pro annual subscription at £14/year** for a personal licence, "other currencies such as US$, Euro, AUS$, JPY are also available" — [SharpCap Pro FAQ](https://www.sharpcap.co.uk/?p=411). An April 2024 forum post references £18/year and a rise from £12 "to offset inflation" — [SharpCap forum](https://forums.sharpcap.co.uk/viewtopic.php?p=42457). **Sources conflict on the current figure; the FAQ (£14) appears newer but this is uncertain.**
- **SharpCap lifetime option:** you can **upgrade from an annual subscription to a lifetime licence**; you must hold an annual licence first, and the lifetime price is discounted by what you have already paid for the year — [SharpCap](https://www.sharpcap.co.uk/?p=1698)
- **SharpCap free tier scope:** "you can still download, install and use most SharpCap functionality for free." Pro-gated features include "the final adjustment stage of polar alignment, live image processing tools like flat frame correction and satellite trail removal" — [SharpCap Pro FAQ](https://www.sharpcap.co.uk/?p=411)
- **SGPro:** annually billed subscription; standard tier **three simultaneous installs**, upgradeable to six; **existing SGPro 2/3 owners keep minor-version upgrades without a subscription** — [SGPro official announcement](https://forum.sequencegeneratorpro.com/t/important-announcement-for-sgpro/13301)
- **FireCapture:** no purchase mechanism found; distributed as free downloads — [firecapture.de](https://firecapture.de)
- **ASIStudio:** distributed free by ZWO, works only with ZWO cameras — [ZWO](https://www.zwoastro.com/2020/03/26/what-is-asistudio-and-what-can-it-bring-us/)

### Inferences — PolyForm Noncommercial vs GPL/MPL/MIT for a hobbyist project seeking contributions
- **[INFERRED] PolyForm Noncommercial is not an open-source licence.** It restricts use by field of endeavour, which the Open Source Definition forbids (OSD clause 6) and which the FSF's Free Software Definition forbids (freedom 0). **[RECALLED]** for the specific clause numbers. The practical consequences follow mechanically:
  - It is **GPL-incompatible**. Code under PolyForm Noncommercial cannot be combined with GPL code, and GPL code cannot be incorporated into a PolyForm Noncommercial project. **ASTAP is GPL** — a hard constraint if library-level integration were ever wanted (process invocation remains fine).
  - **Linux distributions will not package it.** Debian, Fedora and the rest exclude non-free licences from their main archives. ASTAP's presence in Debian and Ubuntu (including armhf) is a direct benefit of its GPL licence that a PolyForm project forgoes.
  - **Package ecosystems and corporate contributors are foreclosed.** A contributor employed by a company generally cannot contribute to a licence their employer's legal review flags as non-commercial-use-only, because the employer cannot use the result.
- **[INFERRED] What PolyForm Noncommercial actually buys:** it prevents a commercial competitor (including a camera manufacturer) from taking the source and shipping it in a product, while still permitting every individual amateur astronomer — the entire actual user base — to use, modify and redistribute it. For an author whose concern is "I do not want ZWO to ship my capture engine," it is precisely targeted, and far simpler than AGPL or a dual-licence arrangement. The explicit naming of "hobby projects, amateur pursuits" and "personal study" in the permitted-purposes definition ([SPDX](https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html)) means an amateur astrophotographer's use is unambiguously covered, including astrophotographers who occasionally sell prints — though that edge is **[GAP]**: the licence text I could access does not resolve whether incidental sale of images produced with the software constitutes commercial use. It almost certainly does not (the software is not being used *commercially*; its output is), but this should be stated carefully.
- **[INFERRED] The comparison against the alternatives:**
  - **MIT** — maximum contribution and adoption, zero protection. A manufacturer can ship it closed.
  - **MPL-2.0** (N.I.N.A.'s choice) — **file-level copyleft**: modifications to existing files must be published, but the software can be linked into proprietary products. This is the pragmatic middle, and it is visibly working: N.I.N.A. has forks, plugins and distribution reach.
  - **GPL-3.0** — strong copyleft, maximum protection among OSI licences, but forecloses proprietary plugin ecosystems and complicates linking against proprietary camera SDKs. **[INFERRED]** This last point is non-trivial in this category: ZWO, QHY and Player One SDKs are proprietary binaries, and GPL linking against them requires a system-library exception argument that is at best uncomfortable. **This may be a substantive reason why astronomy capture software is less GPL-dominated than one might expect.**
  - **PolyForm Noncommercial** — strongest protection against commercial appropriation, at the cost of leaving the open-source ecosystem entirely.
- **[INFERRED]** For the specific goal stated in the brief — "a hobbyist project that wants contributions" — PolyForm Noncommercial is the **weakest** of these choices, because it removes the project from the distribution channels, package ecosystems and corporate-contributor pool through which contributions actually arrive. **MPL-2.0 is the licence that most closely matches that stated goal**, and N.I.N.A. is the working proof within this very category.

### Gaps
- **I could not read the PolyForm Noncommercial licence text directly** (polyformproject.org and spdx.org both unreachable). All findings are from the search index's rendering of the SPDX page. **Verify the full text before publication**, particularly the defensive-termination and notice-requirement clauses, which I could not retrieve.
- **Date conflict unresolved:** SPDX says 9 July 2019; PolyForm's own announcement page is dated May 2020.
- **SharpCap Pro current price unresolved:** £14/year (FAQ) vs £18/year (April 2024 forum post).
- **SGPro subscription price not established.**
- ASTAP's exact GPL version not established.
- PHD2's licence not established in this research.
- No source on FireCapture's formal licence terms (it is distributed free, but the terms — redistribution, reverse engineering — are unknown).

---

## Cross-cutting notes for the report writer

1. **Verification asymmetry is the meta-finding.** Four of six products are closed source with no vendor statement of implementation language. Any architectural ranking of SharpCap, FireCapture, SGPro and ASIStudio rests on inference from installers, error messages and platform matrices. The report should say so once, clearly, rather than implying firmer knowledge than exists.

2. **The GC question should not be resolved in the report.** The evidence genuinely points both ways: the field evidence does not implicate GC, and the theory says the tail is unbounded. Present both, note that the disagreement tracks a values difference (tolerance for rare catastrophic loss) rather than a factual one, and let the reader decide. This is Part A3 and it is the strongest section in these notes.

3. **Three of the brief's "integration" questions turn out not to differentiate the products at all.** Plate solving (child-process + CLI), PHD2 (newline-JSON over TCP), and Alpaca (HTTP/JSON) are deliberately language-neutral protocols. Interop cost (Part A4) is likewise ~0.005% of the frame budget. The report should resist the temptation to score products on these axes — the ecosystem designed them to be unscoreable.

4. **Workload mismatch is a recurring trap.** SGPro and N.I.N.A. are deep-sky sequencers; FireCapture, SharpCap and ASICap are (or include) planetary capture; SharpCap spans both. Plate solving, guiding and FITS matter enormously to the first group and barely at all to the second; SER, 200 fps throughput and preview latency are the reverse. A single league table across all six will mislead unless it is segmented by workload.

5. **Three primary sources must be read before publication:** the SER v3 specification PDF (`SER_Doc_V3b.pdf`, Wilkens & Hahn), the PolyForm Noncommercial 1.0.0 licence text, and the CISA/NSA memory-safe-roadmaps document. None was readable from this environment.
