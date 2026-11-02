// ============================================================
//  LunarFeatures — the named places worth pointing at
//
//  Phase 115. Coordinates and sizes are the IAU Gazetteer of Planetary
//  Nomenclature (planetarynames.wr.usgs.gov), read 2026-11-01:
//  planetographic latitude, longitude positive east, -180 to 180. The
//  size is the feature's diameter or, for a rille, range or sea, its
//  length, which is why Oceanus Procellarum says 2592.
//
//  Generated from the gazetteer's table and not typed: a transposed
//  digit here would send the mount to the wrong crater with no outward
//  sign of it. Chosen for what shows at 0.2"/px, rather than for
//  completeness, and small enough to read as a list.
// ============================================================

#pragma once

namespace raduk::sky {

struct LunarFeature {
    const char* name;
    double latDeg;      // +N
    double lonDeg;      // +E, selenographic: Mare Crisium's side
    double sizeKm;
    const char* kind;
};

inline constexpr LunarFeature kLunarFeatures[] = {
    {"Copernicus", 9.62, -20.08, 96.07, "Crater"},
    {"Tycho", -43.30, -11.22, 85.29, "Crater"},
    {"Plato", 51.62, -9.38, 100.68, "Crater"},
    {"Clavius", -58.62, -14.73, 230.77, "Crater"},
    {"Aristarchus", 23.73, -47.49, 39.99, "Crater"},
    {"Archimedes", 29.72, -3.99, 81.04, "Crater"},
    {"Ptolemaeus", -9.16, -1.84, 153.67, "Crater"},
    {"Alphonsus", -13.39, -2.85, 110.54, "Crater"},
    {"Arzachel", -18.26, -1.93, 96.99, "Crater"},
    {"Theophilus", -11.45, 26.28, 98.59, "Crater"},
    {"Cyrillus", -13.29, 24.07, 98.09, "Crater"},
    {"Catharina", -17.98, 23.55, 98.77, "Crater"},
    {"Kepler", 8.12, -38.01, 29.49, "Crater"},
    {"Gassendi", -17.55, -39.96, 111.39, "Crater"},
    {"Langrenus", -8.86, 61.04, 131.98, "Crater"},
    {"Petavius", -25.39, 60.78, 184.06, "Crater"},
    {"Eratosthenes", 14.47, -11.32, 58.77, "Crater"},
    {"Posidonius", 31.88, 29.99, 95.06, "Crater"},
    {"Eudoxus", 44.27, 16.23, 70.16, "Crater"},
    {"Aristoteles", 50.24, 17.32, 87.57, "Crater"},
    {"Atlas", 46.74, 44.38, 88.12, "Crater"},
    {"Hercules", 46.82, 39.21, 68.32, "Crater"},
    {"Cleomedes", 27.60, 55.50, 130.77, "Crater"},
    {"Endymion", 53.61, 56.48, 122.10, "Crater"},
    {"Pythagoras", 63.68, -62.98, 144.55, "Crater"},
    {"Schickard", -44.38, -55.11, 212.18, "Crater"},
    {"Schiller", -51.72, -39.78, 179.36, "Crater"},
    {"Longomontanus", -49.55, -21.88, 145.50, "Crater"},
    {"Maginus", -50.03, -5.98, 155.58, "Crater"},
    {"Maurolycus", -41.77, 13.92, 115.35, "Crater"},
    {"Stofler", -41.24, 5.93, 129.87, "Crater"},
    {"Walther", -33.25, 0.62, 134.23, "Crater"},
    {"Purbach", -25.51, -2.03, 114.97, "Crater"},
    {"Regiomontanus", -28.28, -1.09, 126.64, "Crater"},
    {"Hipparchus", -5.36, 4.91, 143.95, "Crater"},
    {"Albategnius", -11.24, 4.01, 130.84, "Crater"},
    {"Janssen", -44.96, 40.82, 200.65, "Crater"},
    {"Piccolomini", -29.70, 32.20, 87.58, "Crater"},
    {"Rupes Recta", -21.67, -7.70, 115.95, "Scarp"},
    {"Vallis Alpes", 49.21, 3.63, 155.42, "Valley"},
    {"Montes Apenninus", 19.87, 0.03, 599.67, "Mountains"},
    {"Montes Alpes", 48.36, -0.58, 334.48, "Mountains"},
    {"Montes Caucasus", 37.52, 9.93, 443.51, "Mountains"},
    {"Montes Carpatus", 14.57, -23.62, 333.59, "Mountains"},
    {"Montes Jura", 47.49, -36.11, 420.80, "Mountains"},
    {"Montes Teneriffe", 47.89, -13.19, 111.98, "Mountains"},
    {"Mons Hadley", 26.69, 4.12, 26.40, "Mountains"},
    {"Sinus Iridum", 45.01, -31.67, 249.29, "Bay"},
    {"Sinus Medii", 1.63, 1.03, 286.67, "Bay"},
    {"Sinus Roris", 50.26, -50.86, 195.04, "Bay"},
    {"Mare Imbrium", 34.72, -14.91, 1145.53, "Mare"},
    {"Mare Serenitatis", 27.29, 18.36, 674.28, "Mare"},
    {"Mare Tranquillitatis", 8.35, 30.83, 875.75, "Mare"},
    {"Mare Crisium", 16.18, 59.10, 555.92, "Mare"},
    {"Mare Nectaris", -15.19, 34.60, 339.39, "Mare"},
    {"Mare Humorum", -24.48, -38.57, 419.67, "Mare"},
    {"Mare Nubium", -20.59, -17.29, 714.50, "Mare"},
    {"Mare Fecunditatis", -7.83, 53.67, 840.35, "Mare"},
    {"Mare Frigoris", 57.59, -0.01, 1446.41, "Mare"},
    {"Oceanus Procellarum", 20.67, -56.68, 2592.24, "Mare"},
    {"Mare Orientale", -19.87, -94.67, 294.16, "Mare"},
    {"Mare Marginis", 12.70, 86.52, 357.63, "Mare"},
    {"Mare Smythii", -1.71, 87.05, 373.97, "Mare"},
    {"Mare Humboldtianum", 56.92, 81.54, 230.78, "Mare"},
    {"Grimaldi", -5.38, -68.36, 235.00, "Crater"},
    {"Riccioli", -2.90, -74.42, 155.66, "Crater"},
    {"Hevelius", 2.20, -67.46, 113.87, "Crater"},
    {"Bullialdus", -20.75, -22.26, 60.72, "Crater"},
    {"Wargentin", -49.53, -60.44, 84.69, "Crater"},
    {"Vendelinus", -16.46, 61.55, 141.21, "Crater"},
    {"Furnerius", -36.00, 60.54, 135.03, "Crater"},
    {"Stevinus", -32.49, 54.14, 71.54, "Crater"},
    {"Hyginus", 7.76, 6.27, 8.70, "Crater"},
    {"Triesnecker", 4.18, 3.60, 24.97, "Crater"},
    {"Rima Hyginus", 7.62, 6.77, 203.96, "Rille"},
    {"Rima Ariadaeus", 6.48, 13.44, 247.45, "Rille"},
    {"Rimae Triesnecker", 5.10, 4.83, 200.00, "Rille"},
    {"Rima Hadley", 25.72, 3.15, 116.09, "Rille"},
    {"Vallis Rheita", -42.51, 51.65, 509.07, "Valley"},
    {"Reiner Gamma", 7.39, -58.96, 73.44, "Albedo"},
    {"Wichmann", -7.54, -38.13, 9.77, "Crater"},
    {"Marius", 11.90, -50.84, 40.09, "Crater"},
    {"Bailly", -66.82, -68.90, 300.56, "Crater"},
    {"Moretus", -70.63, -5.95, 114.45, "Crater"},
    {"Mersenius", -21.49, -49.34, 84.46, "Crater"},
    {"Herodotus", 23.25, -49.84, 35.87, "Crater"},
    {"Lambert", 25.77, -20.99, 30.12, "Crater"},
    {"Timocharis", 26.72, -13.10, 34.14, "Crater"},
    {"Autolycus", 30.68, 1.49, 38.88, "Crater"},
    {"Aristillus", 33.88, 1.21, 54.37, "Crater"},
    {"Cassini", 40.25, 4.64, 56.88, "Crater"},
    {"Proclus", 16.09, 46.89, 26.91, "Crater"},
    {"Messier", -1.90, 47.65, 13.80, "Crater"},
    {"Taruntius", 5.50, 46.54, 57.32, "Crater"},
    {"Hortensius", 6.47, -28.00, 14.16, "Crater"},
    {"Fra Mauro", -6.06, -16.97, 96.76, "Crater"},
    {"Bonpland", -8.38, -17.33, 59.25, "Crater"},
    {"Parry", -7.88, -15.78, 47.28, "Crater"},
    {"Davy", -11.85, -8.15, 33.94, "Crater"},
};

} // namespace raduk::sky
