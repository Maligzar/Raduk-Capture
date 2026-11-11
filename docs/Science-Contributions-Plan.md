# Science contributions: what this rig can give the community

Planned 2026-11-11, from the owner's question: how can RadukCapture
contribute directly to science? Phases 141 to 143. Not built.

## What this rig cannot do, and why it matters to say so

The owner brought four areas where professional facilities lead Saturn
research. None of them is reachable from an 8-inch SCT and a visible-light
CMOS camera, and planning as if they were would waste the scarce resource,
clear nights:

| Area | What it needs | Why not here |
|---|---|---|
| Decades of thermal and stratospheric composition (VLT VISIR and MUSE, Keck NIRSPEC) | Mid-infrared imaging and spectroscopy | The sensor sees about 400 to 1000 nm; the stratosphere's signal is thermal infrared |
| Trace gases below the clouds (ALMA) | Millimetre and submillimetre interferometry | A different instrument class altogether |
| Faint irregular moons (CFHT, Subaru, Blanco) | Large apertures, wide fields, magnitudes in the mid-20s | This rig reaches nowhere near that; Saturn has 293 known moons as of 2026 (NASA), nearly all far too faint for it |
| Auroral H3+ emission (JWST NIRSpec, large spectrographs) | Infrared spectroscopy | Same as the first row |

The first area's logic still applies here, though. A Saturnian year is
about 29.5 Earth years, and no spacecraft has watched a whole one. In
visible light, the long record of cloud-top weather is kept largely by
amateurs: PVOL, the amateur planetary image database run at UPV/EHU,
holds tens of thousands of images from more than 300 observers, about
two thirds of them Jupiter and a fifth Saturn (sourced). That is the
door this rig can go through.

## What it can do, ranked by value, fit and timing

### Phase 141: Galilean mutual events, the 2026-27 season (time-critical)

Around each Jupiter equinox, about every six years, the Galilean moons
eclipse and occult one another. IMCCE (Paris Observatory) runs the PHEMU
campaign that collects amateur light curves of these events, and they
refine the moons' orbits. **This season runs from May 2026 to August
2027, with the equinox on 2026-12-16, and most eclipses fall between
October 2026 and January 2027** (IMCCE and BAA, sourced). The next
season is around 2032 (inferred from the six-year cycle).

It suits this rig: the moons are bright, it needs no fine surface detail,
and the app already writes exposure-midpoint timestamps into every SER
(P5.1). What it needs is settings that do not move during the event and a
clock that can be trusted.

The irreplaceable part is the recording. The light curve can be reduced
from the SER files afterwards, so only 141.0 and 141.1 race the season.

| Stage | What | Size |
|---|---|---|
| **141.0** | **Tonight's events.** IMCCE's predictions for the season, filtered to the site: which moons, eclipse or occultation, start, middle and end in UTC, altitude, and how far the pair sits from Jupiter's limb. **Written 2026-11-11.** The source is IMCCE's Opale service (`opale.imcce.fr/api/v1/phenomena/satellites/599`), the API behind its satellites-events form; Multisat now redirects there. The predictions are fetched and cached on the owner's machine, never committed, because IMCCE's terms do not allow its material to be reproduced without permission. Shown on `J` and in the log, with a reminder before each window, rather than in the GoTo planner, which keeps the change small; the planner can follow | S |
| **141.1** | **Event recording.** One action, from the event row or the command palette: AE and autofocus off, gain and exposure locked, an ROI wide enough for both moons and a comparison moon, ROI-following off so the field does not move, recording from about ten minutes before to ten after. **The clock is checked first (B4, promoted from category 6):** if the PC clock cannot be confirmed close to a time server, the recording still runs and its observation record says the time is unconfirmed. IMCCE's requirement, read 2026-11-11 from its observer's guide and technical note 4: every point dated to UTC within 0.1 s, the clock's offset from UTC measured to better than 0.1 s before and after the observation, the PC's own clock not trusted, sampling between 5 points a second and one every 2 seconds, gain constant and nothing saturated, a reference moon in the field. The camera's own delay between exposure and timestamp is not yet measured and must be stated in the record until it is. **Written 2026-11-11** as `Shift+J`: SNTP checks before and after (`TimeServer`), AE, autofocus and ROI-following off and restored, the window recorded, and IMCCE's observation sheet (`fiche-obs-phemu-v4-en.txt`, its own layout and ID rule) written beside the SER as `<ID>.txt`, the unmeasured camera delay stated on it | S-M |
| **141.2** | **The light curve.** Aperture photometry of the two moons and a comparison over the run, from the SER and its timestamps, written in the form IMCCE asks for. It is a scoped piece of roadmap row 33 (stellar photometry): fixed apertures on bright, well-separated discs. Can follow the season, since the SERs keep. **Written 2026-11-11** as `tools/phemu_lightcurve` on `src/core/MoonPhotometry.h`: the pair measured together (IMCCE's "flux sat1&3"), carried through a merge on its measured relative motion, dates corrected from the sheet's clock checks, `<ID>.dat` written beside the sheet. A command-line tool first; choosing the moons by clicking in [PLAY] can follow | M |

### Phase 142: Jupiter impact flashes, through DeTeCt

Small bodies hit Jupiter often enough that amateur video catches the
flashes, and the frequency estimate depends on how many hours of video
were searched, including the ones with nothing in them. DeTeCt, the
open-source detector led by Marc Delcroix, searches amateur Jupiter
videos by differential photometry; the project has gathered a year's
analysis from 262 observers and nearly 360,000 videos, and flagged the
2019 and 2021 flashes (sourced).

RadukCapture should not write its own detector. It already hands
recordings to AutoStakkert; it should hand every Jupiter recording to
DeTeCt the same way, and keep the results with the capture record so a
night's coverage can be reported. Whether DeTeCt reads SER v3 as this
app writes it is to be checked first (unverified).

| Stage | What | Size |
|---|---|---|
| **142.0** | Check DeTeCt against this app's SER files, including 16-bit and the LittleEndian-0 convention | S |
| **142.1** | A DeTeCt handoff after each Jupiter recording, on by default for Jupiter, with the result line stored in the observation record | S |

### Phase 143: records good enough to submit

The cloud-top baseline is built from images whose time, orientation and
longitude can be trusted. PVOL and the ALPO and BAA sections take
processed images with that metadata. The app already names files the way
WinJUPOS expects and writes an observation record; what is missing is the
rest of what a measurer needs.

| Stage | What | Size |
|---|---|---|
| **143.0** | The observation record and FITS header gain the central meridian longitudes (Jupiter System I, II and III; Saturn System III), the filter fitted, and whether the clock was confirmed. The longitudes come from Phase 137 | S |
| **143.1** | A submission sheet after a session: per target, the fields PVOL asks for, filled from the records, for the owner to paste when uploading the processed image. PVOL's actual requirements are to be read from PVOL2 before this is built (unverified) | S |

## Order

141.0 and 141.1 race the season and go first on the bench, after the open
pull requests. They do not depend on Phase 140, since an event recording
locks exposure by hand. Phase 140 follows directly, because every other
kind of contribution here starts with a good image. 141.2, 142 and 143
follow Phase 137, which supplies the longitudes and the event geometry.

## Sources

- IMCCE, PHEMU campaign: https://www.imcce.fr/recherche/campagnes-observations/phemus/phemu
- BAA, mutual events of the Galilean satellites 2026-27: https://britastro.org/section_news_item/mutual-events-of-the-galilean-satellites-2026-27
- Small impacts on the giant planet Jupiter (arXiv 1804.03006): https://arxiv.org/pdf/1804.03006
- EPSC2024-1178, DeTeCt project results: https://meetingorganizer.copernicus.org/EPSC2024/EPSC2024-1178.html
- Europlanet, the 2019 impact flash: https://www.europlanet-society.org/?p=7011
- PVOL2 (arXiv 1701.01977): https://arxiv.org/pdf/1701.01977v1
- NASA, Saturn's moons: https://science.nasa.gov/saturn/moons/
