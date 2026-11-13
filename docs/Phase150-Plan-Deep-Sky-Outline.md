# Phase 150: deep sky and stars, an outline

**Moved after release 4.0 (P3) by the owner, 2026-11-13**, following the strategic review's advice: deep sky waits until the planetary product has proven itself, so the app does not compete with mature deep-sky tools before its core is reliable. 149.2 and 149.3 move with it. Nothing in 146 to 149 stops building to fit it.

This is an outline, not a plan. It is written 2026-11-13 so that phases 146 to 149 are built to fit it, and it is to be planned properly when 149's single exposures exist. Status: `OUTLINED`.

## What deep-sky imaging asks that planetary does not
- **Long single exposures, many of them.** A light sequence of N subs at a fixed exposure, gain, offset, binning and temperature.
  - Built on 149.3's single exposures and 149.2's cooling.
  - Saved as FITS at the native depth, one file per sub, with the header deep-sky tools read: `EXPTIME`, `GAIN`, `OFFSET`, `CCD-TEMP`, `FILTER`, `XBINNING`, the WCS once solved, and `IMAGETYP`.
- **Calibration frames.**
  - Darks (matched exposure, gain and temperature), flats (auto-exposed to a target level), bias or dark-flats.
  - A library kept by those keys, so a light sequence can say which calibration it needs and whether it already exists.
- **Guiding.**
  - The rig's planetary guiding steers on the planet's centroid. Deep sky guides on a star through a guide camera (149.4) or off-axis.
  - The rate commands, calibration matrix and `GuideSolve` from phases 51 and 133 are reused, fed by a star centroid instead of a disc.
- **Dithering.** Small random offsets between subs, waiting for guiding to settle.
- **Plate solving drives the framing.** ASTAP is already integrated (phase 47b and others). Deep sky uses it to centre and to re-frame after a meridian flip, which needs an equatorial mount; see below.
- **Focus on stars.**
  - Phase 31/32's HFD and FWHM, measured over many stars.
  - Phase 145's two-stage autofocus fits: HFD for coarse and fine on stars, the edge metric staying planetary and lunar.
  - Per-filter offsets (149.5) and refocusing on temperature drift.
- **Photometry and annotation.** Rows 33 and 43, unstarted, belong here.

## The mount is the limit, not the camera
The NexStar 8SE is alt-az. Exposures longer than about 30 to 60 s show field rotation, and that limit depends on where in the sky the target is (inferred; to be worked out per target when planned). The roadmap backlogged polar alignment (row 16) "until a wedge, and probably a new mount". So, with this mount, deep sky means short subs:
- a few seconds to about 30 s, many of them;
- derotation in stacking;
- which the 678's low read noise suits ("lucky deep-sky"; inferred).

If the owner adds an equatorial mount (ZWO makes AM3 and AM5 strain-wave mounts; driven through ASCOM or ZWO's own protocol, to be checked when chosen), long exposures, meridian flips and polar alignment come in.

## Stacking
Out of scope by direction. The quick-look stacker (phase 74) and handing files to AutoStakkert, Siril or PixInsight stay the model, just as AutoStakkert is for planets. Live stacking was dropped (row 17) and is not revived here.

## Order
149.0, 149.1 and 149.3 first. Then 150 as a plan of its own:
1. light sequences and the FITS header;
2. calibration;
3. star focus;
4. guiding on a star (needs 149.4);
5. dithering.
