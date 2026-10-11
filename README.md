# hpvis

A fast, GPU-accelerated viewer for [HEALPix](https://healpix.sourceforge.io/)
maps. The projection and the HEALPix pixel lookup run per pixel in a fragment
shader, so large maps (nside 2048 and above) stay responsive.

## Features

- Five projections: Mollweide, Plate Carrée, Orthographic, Azimuthal
  equidistant and Gnomonic, with interactive sky rotation.
- RING and NESTED pixel ordering.
- 15 colormaps plus your own (CSV or JSON), linear and logarithmic scales, an adjustable color range and a
  color bar.
- Masks: load several, toggle, invert, and hide or dim the masked pixels.
- Graticule, minimap, zoom, pan and touchpad gestures.
- Several maps open at once in tabs, with a toolbar for HDU, column,
  projection and colormap.
- Map statistics, a FITS header viewer, power spectrum (via NaMaster) and
  image export.
- Settings dialog for the defaults of new maps.

## Building

Requirements: a C++20 compiler, CMake 3.22+, Qt 6 (Core, Gui, Widgets,
OpenGLWidgets, Concurrent, Graphs), OpenMP and cfitsio. HEALPix C++ and
libsharp are built from the vendored sources as part of the normal build.

```sh
git clone --recurse-submodules <repository-url> hpvis
cd hpvis
cmake -B build -S .
cmake --build build -j
./build/hpvis
```

The first build is slower because it compiles HEALPix.

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `WITH_NAMASTER` | `ON` | Build NaMaster (power spectrum support) |
| `CMAKE_BUILD_TYPE` | `Release` | Build type |

## Usage

```sh
hpvis [options] [FILE...]
```

| Option | Description |
| --- | --- |
| `-H, --hdu N` | HDU to show (1-based, default: first map HDU) |
| `-c, --column N` | Column to show (1-based, default: first) |
| `-m, --colormap NAME` | e.g. `viridis`, `inferno`, `jet`, a custom colormap name, or a colormap file |
| `-r, --reverse` | Reverse the colormap |
| `-s, --scale linear\|log` | Color scale |
| `--min V`, `--max V` | Color range |
| `-p, --projection P` | `mollweide`, `plate_carree`, `orthographic`, `azimuthal_equidistant`, `gnomonic` |
| `-g, --graticule` | Show the coordinate grid |
| `--mask FILE` | Apply a mask (repeatable) |
| `-e, --export FILE` | Save the view as an image and exit |
| `-i, --info` | List HDUs and columns and exit (no window) |
| `--stats` | Print min, max, mean, rms and std and exit (no window) |

Examples:

```sh
hpvis samples/wmap_W_iqu_nside32.fits
hpvis -p orthographic -m inferno -g map.fits
hpvis --mask mask.fits --export out.png map.fits
hpvis --info map.fits
```

Files can also be opened from File > Open or by dropping them on the window.

## Controls

| Action | Input |
| --- | --- |
| Zoom | Mouse wheel, pinch, or Ctrl + scroll (toward the cursor) |
| Pan | Left-drag, or two-finger scroll |
| Rotate the sky | Ctrl + left-drag or middle-drag (plain left-drag in Orthographic and Azimuthal views) |
| Zoom to a region | Shift + left-drag |
| Reset view | Double-click, or `0` |
| Toggle minimap | `M` |
| Toggle color bar | `C` |
| Open / close tab | `Ctrl+O` / `Ctrl+W` |

## Custom colormaps

Add your own colormaps as CSV or JSON files, either with
**View > Custom Colormaps > Load Colormap...** (the file is copied into the
colormaps folder) or by putting them in that folder yourself
(**View > Custom Colormaps > Open Colormaps Folder**, `colormaps/` in the
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
the recent files list under `~/.local/share/hpvis/`.

See [CHANGELOG.md](CHANGELOG.md) for the list of changes.
