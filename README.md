# hpvis

A fast, GPU-accelerated viewer for [HEALPix](https://healpix.sourceforge.io/)
maps. The projection and the HEALPix pixel lookup run per pixel in a fragment
shader, so large maps (nside 2048 and above) stay responsive.

## Screenshots

![Home](./images/home.png)

![Orthographic](./images/orthographic_projection.png)

![Map Expression Evaluation](./images/experssion_engine.png)

## Features

- Galactic, Ecliptic and Equatorial orientations (converted on the GPU from
  the map's own `COORDSYS`), or a free trackball.
- Eleven projections: Mollweide, Hammer, Equirectangular, Mercator, Sinusoidal,
  Lambert cylindrical, Gall-Peters, Orthographic, Azimuthal equidistant,
  Gnomonic and Stereographic, with interactive sky rotation, seen from the
  inside (the sky) or the outside (like a globe).
- RING and NESTED pixel ordering.
- 15 colormaps plus your own (CSV or JSON), linear and logarithmic scales, an adjustable color range and a
  color bar.
- Masks: load several, toggle, invert, and hide or dim the masked pixels.
- Graticule, minimap, zoom, pan and touchpad gestures.
- FITS, NumPy `.npy` and `.npz` files. The toolbar chooses the HDU and
  column of a FITS file, or the array and row of a `.npz` (a `.npy` has just
  the row).
- Several maps open at once in tabs, with a toolbar for HDU, column,
  projection and colormap.
- A file explorer dock on the left (View > File Explorer) to browse folders
  and open maps, and drag and drop of files onto the window.
- Sessions: the files that were open when you quit come back at the next
  start (can be turned off in the settings).
- Operations on maps: add, subtract, multiply, divide or any expression of
  several maps, with the result opened as a new map and saved as FITS.
- Map statistics, a FITS header viewer, power spectrum and image export.
  The power spectrum and the histogram plots export to PNG, or to SVG and PDF
  as vector graphics, in the colors on screen or black on white (Settings >
  Appearance).
  The power spectrum is corrected for the masks on the map (MASTER method,
  with NaMaster's coupling coefficients), with optional mask apodization,
  bandpowers and E/B purification.
- Tools > Maps > Derivatives and E/B Maps: Laplacian, gradient magnitude, and E and B
  maps of a polarized map, as new tabs.
- Colormap > Color Bar > Percentile Range: auto-range that clips the extreme pixels.
- Settings dialog for the defaults of new maps.
- Menus grouped by topic (View, Colormap, Masks, Tools), with icons, and a key
  binding for almost every command (Tools > Settings > Shortcuts).


## Documentation

The full usage guide is in [docs/USAGE.md](docs/USAGE.md) and inside the
program: Help > Usage (`F1`).

## Installation

See [INSTALLATION.md](INSTALLATION.md) for dependencies, build options and
install instructions.

## Usage

```sh
hpvis [options] [FILE...]
```

| Option | Description |
| --- | --- |
| `-H, --hdu N` | HDU to show (1-based, default: first map HDU); for a `.npz`, the array number listed by `--info` |
| `-c, --column N\|NAME` | Column to show, by number (1-based) or name such as `TT` (default: first); for a 2-D `.npy` / `.npz` array, the row number |
| `-m, --colormap NAME` | e.g. `viridis`, `inferno`, `jet`, a custom colormap name, or a colormap file |
| `--creverse` | Reverse the colormap |
| `--cscale linear\|log\|asinh` | Color scale |
| `--cmin V`, `--cmax V` | Color range |
| `-p, --projection P` | `mollweide`, `equirectangular`, `orthographic`, `azimuthal_equidistant`, `gnomonic`, `mercator`, `sinusoidal`, `lambert_cylindrical`, `gall_peters`, `stereographic`, `hammer` |
| `--orientation O` | `free` (default), `galactic`, `ecliptic` or `equatorial`: the frame to show the sky in |
| `--view inside\|outside` | Perspective: the sky seen from the inside (longitude increases to the left, the default) or from the outside like a globe (mirrored) |
| `-g, --graticule` | Show the coordinate grid |
| `--mask FILE` | Apply a mask (repeatable) |
| `-o, --output FILE` | Save the view as an image and exit |
| `-i, --info` | List HDUs and columns (for a `.npz`, the arrays and rows) and exit (no window) |
| `--stats` | Print min, max, mean, rms and std and exit (no window) |

Examples:

```sh
hpvis samples/wmap_W_iqu_nside32.fits
hpvis -p orthographic -m inferno -g map.fits
hpvis --mask mask.fits --output out.png map.fits
hpvis --info map.fits
hpvis --hdu 2 --column 3 maps.npz
```

Files can also be opened from File > Open, from the file explorer dock, or
by dropping them on the window. Without files on the command line, the
files of the last session are reopened (see Sessions below).

## NumPy files

A `.npy` holds one array and a `.npz` is an archive of arrays. An array is a
map when it is 1-D with `12 * nside^2` values or 2-D with one such map per
row (`float` and integer types, little-endian). In a `.npz`, only the arrays
that hold a map are listed in the toolbar's Array chooser (named by their
key); the Row chooser picks a row of a 2-D array. Masks and map operations
read them too. NumPy files have no header, so the frame is assumed to be
Galactic.

A `.npz` array that needs more than 4 GiB of memory (compressed archives can
be tiny and still hold huge arrays) is only loaded after you confirm.

## Power spectrum and masks

The Power Spectrum dock computes the spectra of the map (TT, and TE, EE, BB,
TB, EB when the HDU has Q and U columns). With a mask enabled on the map
(Masks menu), "Correct for the mask" applies it as a weight and corrects the
spectra for it with the MASTER method: the pseudo-C_l of the masked map are
decoupled with the mode-coupling matrix of the mask, in bandpowers of
Δℓ multipoles (shown at their effective multipole).

- **Apodization** (C1, C2 or Smooth, with a scale in degrees) smooths the
  edge of the mask, which reduces the leakage between scales. NaMaster's
  apodization is slow for big maps: it is refused above nside 1024.
- **Purify E / Purify B** remove the leakage of E modes into B (and the
  reverse) caused by the mask (Smith & Zaldarriaga 2007). They need Q and U
  and an apodized mask, cost more time and memory, and raise the noise of the
  purified mode.
- The coupling matrix grows with lmax²: with a mask, lmax is limited to 4096
  (2048 with polarization), and nside to 2048.
- "Bin theory" (Compare menu: Theory) draws the theory spectrum convolved with
  the bandpower windows, which is what the bandpowers estimate; without it the
  theory is drawn per multipole.
- Unseen pixels get no weight, so they are corrected for like the mask.
- The spectra can be exported to CSV (bandpowers at ℓ_eff when corrected).

Without the correction the masks are ignored by the spectrum and the dock
warns about it.

**Cross-spectrum.** Compare > Cross-spectrum (TT) with... adds the cross-spectrum
C_l^AB of the current map and another open map of the same nside as a curve.
For two maps with the same sky and independent noise (for example half-mission
splits) the noise cancels, so it is a null test with no noise bias to
subtract. The masks on the current map weight both maps, and the result is
divided by the mean squared weight: this is accurate for l well above the
size of the mask but does not remove the mode coupling like the MASTER
correction.

## Derivatives and E/B maps

Tools > Maps > Derivatives and E/B Maps works through the spherical harmonics and
opens the result as a new tab:

- **Laplacian**: each a_lm is multiplied by -l(l+1). This boosts small
  scales, so noise grows quickly; lower the analysis l max to suppress it.
- **Gradient magnitude** |∇T|, in map units per radian.
- **E and B maps**: splits the polarization (Q, U) of the map's HDU into its
  gradient-like (E) and curl-like (B) parts, opened as two tabs. On a cut sky
  some E leaks into B near the edge of the mask.

UNSEEN pixels are taken as zero in the transform and restored in the result.

## Sessions

When the window closes, the files in the open tabs are remembered with their
HDU / array and column / row and the current tab. At the next start without
files on the command line they are reopened in the same order; files that no
longer exist are skipped. Turn this off in Settings > Files, or reopen the
last session on demand with File > Reopen Last Session. Computed maps and
per-map view options (colormap, range, ...) are not part of the session.

## Controls

| Action | Input |
| --- | --- |
| Zoom | Mouse wheel, pinch, or Ctrl + scroll (toward the cursor) |
| Pan | Left-drag, or two-finger scroll |
| Rotate the sky | middle-drag (plain left-drag in the Orthographic, Azimuthal, Gnomonic and Stereographic views) |
| Zoom to a region | Shift + left-drag |
| Statistics of a region | Ctrl + Shift + left-drag for a rectangle, or View > Statistics Region for a rectangle, ellipse, polygon (click corners, double click or Enter to close, Esc cancels, right click removes the last corner) or line (great-circle arc, drag between its ends); shown in the Statistics dock |
| Reset view | Double-click, or `0` |
| Toggle minimap | `M` |
| Toggle color bar | `C` |
| Open / close tab | `Ctrl+O` / `Ctrl+W` |
| Open a file from the explorer | Double-click or Enter; or drag it onto the window |

## Custom colormaps

Add your own colormaps as CSV or JSON files, either with
**Colormap > Custom Colormaps > Load Colormap...** (the file is copied into the
colormaps folder) or by putting them in that folder yourself
(**Colormap > Custom Colormaps > Open Colormaps Folder**, `colormaps/` in the
app's config directory). They are picked up at startup and appear after the
built-in ones in the toolbar and in Settings. `--colormap` takes their name or
a file path. You can have up to 64.

CSV: one color per line, as `r,g,b` (0-1 or 0-255) or `#rrggbb`. A header line
and `#` comments are ignored. Colors are spread evenly over the range.

```csv
r,g,b
0,0,0
255,0,0
255,255,0
255,255,255
```

JSON: a name and a list of colors, each a `"#rrggbb"` string, an `[r, g, b]`
array, or `{"position": 0-1, "color": ...}` for uneven spacing (all or none
of the colors need a position).

```json
{
  "name": "Ice",
  "colors": [
    {"position": 0.0, "color": "#000020"},
    {"position": 0.3, "color": [0.0, 0.5, 1.0]},
    {"position": 1.0, "color": "#ffffff"}
  ]
}
```

Colors between the entries are interpolated linearly. At least two colors are
needed.

## Samples

`samples/` has small test maps and masks (WMAP and Planck dust data, and
synthetic maps at nside 32 up to 8192).

## Settings

Preferences are stored with `QSettings` (on Linux, under `~/.config`), and
the recent files list under `~/.local/share/hpvis/`. The last session and the
file explorer's folder are stored with the preferences.

## Caveats

- Maximum nside is 8192. The map is kept on the GPU as 32-bit floats, 4 bytes
  per pixel: nside 2048 needs 0.2 GB, nside 4096 needs 0.8 GB and nside 8192
  needs 3.2 GB of video memory (integrated GPUs share system memory). Maps
  with a larger nside than the "Stream maps above nside" setting (Settings >
  General, default 4096) are streamed instead: a coarse copy (at most nside
  2048) stays on the GPU and the full-resolution tiles on screen are loaded as
  you zoom and pan, which needs only a few hundred MB whatever the nside. The
  map is also held in RAM in full (3.2 GB at nside 8192).
- Maps are stored and displayed as 32-bit floats (`Healpix_Map<float>`), also
  when the file holds 64-bit values, which are rounded on load. Double
  precision would double the memory use (6.4 GB at nside 8192) and the GPU
  upload is 32-bit anyway. Spherical harmonic transforms (smoothing,
  resolution changes, power spectra) and sums for statistics are computed in
  double precision.
- While streaming, zooming in shows the coarse map for a moment until the
  tiles arrive, and masks are applied at the coarse resolution.
- The GPU's maximum texture size (usually 16384) also caps nside when not
  streaming.
- If the GPU cannot allocate the textures, a message is shown and the map is
  not displayed; lower the streaming limit in the settings.

## License

hpvis is licensed under the [GNU Affero General Public License, version 3](LICENSE).
Help > About lists the libraries it uses and shows the license text.

See [CHANGELOG.md](CHANGELOG.md) for the list of changes.
