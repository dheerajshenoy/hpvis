# TODO

## Rendering
- [x] Tiled rendering for large maps (streaming above a configurable nside)
- [ ] Smooth/bilinear interpolation option
- [ ] HDR

## Color scale
- [ ] Percentile clipping (1%/99%) and auto-range button

## Interaction
- [ ] Measure angular distance between two points
- [ ] Region selection with stats (mean, std, min, max)
- [ ] Drag and drop map as images to other apps

## Overlays
- [ ] Markers / catalog overlay (load from CSV)
- [ ] Text annotations

## Data & files
- [ ] Polarization display (Q/U → amplitude, vectors)
- [ ] Map operations: difference, ratio, smoothing, ud_grade
- [ ] Histogram view of map values

## Export
- [ ] Copy view to clipboard

## UI / App
- [ ] Side-by-side / linked views (synced zoom, rotation, color range)
- [ ] Packaging (AppImage / Arch PKGBUILD) and README with screenshots

## Quality
- [ ] Unit tests for projection and index math
- [ ] CI build (CMake + Qt6 + cfitsio + healpix_cxx)
- [ ] macOS fallback path (OpenGL 4.1 core profile check)

### Projection & view
- [ ] Projection combo (Mollweide, Hammer, Gnomonic, Orthographic)
- [ ] Flip longitude toggle

### Data
- [ ] Column/field selector (TT, QQ, UU, ...)
- [ ] Smoothing FWHM spinbox

### Analysis
- [ ] Theoretical Model Overlay: Allow users to load or compute theoretical $C_\ell$ curves (e.g., from CAMB or Planck best-fit parameters) to compare directly against their map data.
- [ ] Map-Level & Statistical DiagnosticsTools to inspect the raw properties of the loaded HEALPix map:Pixel Distribution Histogram: A live histogram of pixel values compared against a Gaussian distribution (useful for detecting foreground contamination or non-Gaussianities).High-Order Statistics: Real-time computation of Skewness and Kurtosis to check for asymmetry or heavy tails in the temperature fluctuations.Interactive Cursor Readout: Display coordinates (Galactic $l/b$ or Equatorial RA/Dec) and exact temperature/polarization values ($\mu\text{K}$ or $\text{K}$) instantly as the user hovers over any pixel.
