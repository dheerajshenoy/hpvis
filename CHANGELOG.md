# CHANGELOG

## 0.1.0

First development release.

### Rendering
- GPU Mollweide projection of HEALPix maps, computed per pixel in a fragment
  shader (inverse projection plus HEALPix pixel lookup).
- Support for both RING and NESTED pixel ordering, selectable from the side
  panel.
- Longitude increases to the left, matching healpy.
- Pixel indices use integer math (`texelFetch`) so large `nside` values stay
  exact.
- The map is packed into a 2D texture, avoiding the 1D texture size limit.
- Pixels marked `UNSEEN` (and NaN/Inf) are drawn in a configurable color.
- Shaders are split into small files (`common`, `colormaps`, `healpix_ring`,
  `healpix_nest`, `projections`, `scale`, `masks`), assembled with an
  `#include` loader, and embedded in the binary as Qt resources.
- Shader compile and link errors are printed to the console.
- The map uploads to the GPU without an intermediate CPU copy, and happens
  when a file is opened rather than the first time its tab is shown.

### Color
- 15 colormaps: grayscale, inferno, magma, plasma, viridis, cubehelix, jet,
  HSV, hot, cool, spring, summer, autumn, winter and rainbow.
- Reverse colormap option.
- Adjustable color range (min/max) with a "Reset Range" button that restores
  the range of the data.
- Linear and logarithmic color scales. If the data range includes values <= 0,
  the log scale uses a log-style stretch so it stays defined.
- Configurable background color (outside the ellipse) and unseen-pixel color.

### Navigation
- Zoom (1x to 50x) and pan, always zooming toward the cursor.
- Mouse wheel zoom, left-drag pan, double-click to reset the view.
- Touchpad support: pinch to zoom, two-finger scroll to pan, Ctrl+scroll to
  zoom, smart-zoom gesture to reset.
- Optional minimap (View > Minimap) in the bottom-right corner, shown only
  while zoomed in. A red box marks the visible region; click or drag inside
  it to recenter the view.
- The OpenGL viewport accounts for the display scale factor, so the anchor
  point stays under the cursor on scaled (HiDPI) screens.

### Masks
- Load one or more HEALPix mask files, and add more at any time.
- Each mask has its own checkbox in the side panel and a button to remove it;
  "Clear" removes all of them.
- Per-mask "Inv" checkbox inverts a mask (1 − m), for files that store 1 for
  "masked" instead of "keep". Like enabling, it only changes a uniform.
- Enabled masks are combined (product) on the GPU. Toggling a mask only
  changes a uniform and does not re-upload any data.
- Masks are stored as 8-bit values and packed four to an RGBA8 texture; up to
  16 masks can be loaded.
- "Hide" mode paints masked pixels in the unseen color; "Dim" mode fades them
  with an adjustable strength.
- Masks must match the map's `nside`; a mismatch shows a warning and the file
  is skipped. A mask with a different pixel ordering is converted.
- The implicit color range ignores masked pixels while masks are enabled.
- Loading a map of a different size drops the loaded masks.

### Color bar
- Optional color bar (View > Color Bar) with tick marks and labels. The tick
  count adapts to the width, and labels use a compact or scientific format.

### Interface
- Tabbed interface: open several maps at once, each with its own view and
  settings.
- Side panel with collapsible sections (Pixel Ordering, Color, Overlays) in a
  scroll area.
- Panel controls follow the current tab when you switch tabs.
- "Map Info" section (nside, npix, minimum, maximum, average, RMS, standard deviation). The statistics are computed
  once on load and whenever masks change, so switching tabs with large maps
  stays fast. They only include pixels kept by the enabled masks.
- Sky fraction (f_sky) shown for the enabled masks.
- View menu toggles for the side panel and the minimap.
- Full-width status bar showing the file name, file size, zoom level, and the
  cursor's longitude/latitude, pixel index and pixel value.
- File > Open Recent lists the last 10 opened files (newest first, with a
  "Clear Recent Files" entry). The list is saved in
  `recent_files.txt` in the app data directory
  (`~/.local/share/Hpixview/` on Linux).
- Open files from the File menu, or from the command line (`Hpixview
  file.fits ...`).
- Files are opened with `fitshandle`: every HDU that holds a HEALPix map (NSIDE and
  ORDERING keywords, columns of 12*nside^2 values) is listed in the Map Info section (HDU and Column), and switching either one
  reloads that map in the background. The first map HDU / column is used
  when a file is opened.
- "Header" button in the menu bar opens a dialog with the FITS header cards
  (keyword, value, comment) of any HDU in the current file, with a filter box
  and a Copy button.
- Maps and masks are read on background threads, so the window stays
  responsive while large files load (the status bar shows "Loading...").
  Several files opened together load in parallel.
- Map statistics (range, f_sky, average, RMS, standard deviation) and the mask
  weight conversion run on all CPU cores.
- Error dialog when a map cannot be loaded, including HEALPix library errors.

### Build and packaging
- CMake build with Qt 6, vendored cfitsio and HEALPix, and OpenMP linked.
- Sample data in `samples/` (WMAP nside 32, Planck dust nside 2048, and two
  test masks).
