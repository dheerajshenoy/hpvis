# TODO

## Rendering
- [ ] Integer uniforms / 2D index math for nside > 1024 (float precision)
- [ ] Check shader compile errors (`addShaderFromSourceCode` return values) and show them in a dialog
- [ ] Smooth/bilinear interpolation option
- [ ] Mask bad pixels (`UNSEEN`) with a distinct color
- [x] Verify HEALPix RING lookup against `Healpix_Map::ang2pix` (spot-check values)
- [x] NESTED ordering support (convert on load or add `ang2pix_nest` in shader)
- [x] Fix east/west orientation (longitude increasing to the left, healpy style)
- [x] Embed shaders as Qt resources instead of loading from `applicationDirPath()/../shaders`

## Color scale
- [ ] Colorbar widget with tick labels
- [ ] Percentile clipping (1%/99%) and auto-range button
- [x] Colormap selection (viridis, plasma, coolwarm, planck, grayscale)
- [x] User-adjustable min/max (spinboxes + sliders)
- [x] Scaling modes: linear, log, asinh, histogram-equalized

## Projections & navigation
- [ ] Additional projections: Gnomonic, Orthographic, Cartesian, Hammer-Aitoff
- [ ] Zoom and pan (mouse wheel / drag)
- [ ] Rotate view (lon/lat center), reset view
- [ ] Coordinate system conversion (Galactic / Equatorial / Ecliptic)

## Interaction
- [ ] Cursor readout: lon/lat, pixel index, value (status bar)
- [ ] Pixel picking via CPU `ang2pix` on mouse click
- [ ] Measure angular distance between two points
- [ ] Region selection with stats (mean, std, min, max)

## Overlays
- [ ] Graticule (lon/lat grid) with labels
- [ ] Markers / catalog overlay (load from CSV)
- [ ] Mask overlay
- [ ] Text annotations

## Data & files
- [ ] Multi-column FITS support (choose TT/QQ/UU, etc.) via a column selector
- [ ] Show FITS header metadata (dock/dialog)
- [ ] Polarization display (Q/U → amplitude, vectors)
- [ ] Map operations: difference, ratio, smoothing, ud_grade
- [ ] Histogram view of map values
- [ ] Recent files menu, drag-and-drop to open
- [ ] Error dialog on failed load (bad path, non-HEALPix FITS)

## Export
- [ ] Save current view as PNG/PDF (`grabFramebuffer`)
- [ ] High-resolution offscreen export
- [ ] Copy view to clipboard

## UI / App
- [ ] Close tabs, tab context menu (duplicate, close others)
- [ ] Side-by-side / linked views (synced zoom, rotation, color range)
- [ ] Settings persistence (QSettings: last dir, colormap, window geometry)
- [ ] Keyboard shortcuts
- [ ] Wire up Edit and View menus (currently empty)
- [ ] Fix `~` expansion and relative paths for command-line files
- [ ] Packaging (AppImage / Arch PKGBUILD) and README with screenshots

## Quality
- [ ] Unit tests for projection and index math
- [ ] CI build (CMake + Qt6 + cfitsio + healpix_cxx)
- [ ] macOS fallback path (OpenGL 4.1 core profile check)

## Side panel
Group into collapsible sections (QGroupBox / toolbox).

### Display
- [x] Colormap combo (viridis, plasma, coolwarm, Planck, grayscale) + reverse checkbox
- [x] Min/max spinboxes, Auto button, percentile presets (100/99/95%)
- [x] Scale combo (linear, log, asinh, histogram-equalized)
- [ ] Bad pixel (`UNSEEN`) color picker
- [ ] Background color picker (outside the ellipse)

### Projection & view
- [ ] Projection combo (Mollweide, Hammer, Gnomonic, Orthographic)
- [ ] View center lon/lat spinboxes and rotation slider
- [ ] Coordinate system combo (Galactic, Equatorial, Ecliptic)
- [ ] Flip longitude toggle
- [ ] Reset view button

### Data
- [ ] Column/field selector (TT, QQ, UU, ...)
- [ ] Read-only map info: nside, npix, ordering, coord system, min/max/mean/std
- [ ] Default pixel ordering from FITS header (`ORDERING`) instead of manual choice
- [ ] Smoothing FWHM spinbox

### Overlays
- [ ] Graticule checkbox + spacing spinbox
- [ ] Colorbar toggle
- [ ] Load mask file + opacity slider

### Cursor & export
- [ ] Cursor readout box (lon/lat, pixel index, value)
- [ ] Save image button with resolution choice
