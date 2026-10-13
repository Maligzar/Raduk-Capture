# Is the histogram accurate?

Asked directly after a long emulator session. Answered here on the
bench, 2026-10-13, by driving the real `FrameAnalyzer` with synthetic
frames whose answers can be worked out by hand — `tests/histogram_accuracy.cpp`,
which runs as part of `tests\run-tests.bat`.

**Short answer: the arithmetic is correct. Two things about it are worth
knowing, and one of them affects the display's main job.**

---

## What is correct

**Channel assignment.** Given a frame with a different value at each
Bayer position (R=40, Gr=80, Gb=120, B=200), R reports 40 and B reports
200. There is no half-pixel error, so colour balance is not mislabelled.

**Clip percentages.** A fully saturated frame reads 100% on every
channel. Saturate only red and only red reports it — 100% R, 0.0% B. The
figure is the percentage of *sampled* pixels at 255, which is the right
definition.

**16-bit.** ASI RAW16 is left-aligned in the 16-bit word, and the code
reduces it with `>> 8`. A 12-bit sensor at full scale is `4095 << 4` =
65520, and that reads as clipped. Mid-scale `2048 << 4` lands at exactly
128 in 8-bit terms and reports no clipping. The left-align handling is
right, including the historical trap: an earlier `>> 4` left values at
0–4095 which the `uint8_t` cast then wrapped, garbling the histogram in
16-bit mode entirely.

---

## Two things worth knowing

### 1. The G channel is the mean of Gr and Gb

A Bayer quad has two greens. The analyser averages them before the
histogram sees anything:

```cpp
uint8_t gV = (uint8_t)(((uint16_t)g1V + g2V) >> 1);   // avg G
```

Measured consequence: a frame with **Gr=20 and Gb=180** produces a G
histogram occupying **one bin**, at 100 — identical to a frame with
Gr=Gb=100. A 160-count split between the two greens is completely
invisible.

Two things follow, and the second is the one that matters:

- **Gr/Gb imbalance cannot be seen.** Most colour sensors have some,
  from crosstalk differences between the green next to red and the green
  next to blue. If it ever became large — a sign of something wrong — the
  histogram would not show it.
- **G's distribution is artificially narrower than R and B.** Averaging
  two samples reduces noise by about √2, and only G gets that treatment.
  So the G row always looks tighter than R and B for a reason that is
  not the sensor's colour response.

That second point lands squarely on what the panel is for. The reason
per-channel histograms were added was to read colour balance off how far
each row extends, and one of the three rows is being quietly denoised
relative to the others.

**Not changed**, because it alters a display in daily use and the right
answer depends on what you want from it. Options, cheapest first:

| Option | Effect |
|---|---|
| Use Gr only | G becomes directly comparable to R and B. Loses half the green samples. |
| Draw Gr and Gb as separate rows | Four rows; makes imbalance visible; busier. |
| Keep the average, label it | No code change; stops the comparison being read as more precise than it is. |

### 2. It measures the raw frame, not the calibrated one

`analyzer.Submit(frame)` is handed the ring-buffer slot directly.
Calibration happens later and elsewhere: `PreviewRenderer` applies dark
and flat into its own `m_calBuf` scratch buffer and leaves `frame->data`
untouched.

So with dark/flat enabled, **the histogram describes the sensor and the
preview describes the corrected image, and they are not the same
picture.**

This is defensible and probably right: saturation is a fact about the
sensor that no amount of calibration undoes, so clip percentages
*should* come from raw data. But it is undocumented, and "accurate"
depends on which question is being asked. A flat that lifts the corners
by 30% changes what you see without changing the histogram.

---

## One detail: it samples 6.25% of the frame

The sampler steps 4 pixels in both axes, so it reads one superpixel in
four, in each axis — **4096 samples from a 256×256 frame**. The comment
calls this "every other superpixel for speed" and it is deliberate.

Fine for the shape of a distribution. Worth knowing for clip: samples
sit 4 pixels apart, so a saturated region smaller than about 4 pixels
can fall between them and go unreported. On a planet, saturation is a
contiguous area far larger than that, so this is a footnote rather than
a problem.

---

## What still needs sky

The arithmetic is settled. What the bench cannot answer:

- **Does it match a real sensor?** Real read noise, real Gr/Gb imbalance,
  real saturation behaviour near full well. The emulator has none of
  these, so every number above was verified against frames with no noise
  at all.
- **Is the 99.9th-percentile highlight the right statistic?** Auto-exposure
  now drives `highlightDN` rather than the frame mean. That percentile
  assumes the planet is at least 0.1% of the frame; true for Jupiter and
  Saturn at this focal length, marginal for a small target.

Both are on the clear-night list as item 11.
