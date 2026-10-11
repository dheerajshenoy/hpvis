# CHANGELOG

## 0.1.0

First development release.

### Rendering
- GPU Mollweide projection of HEALPix maps, computed per pixel in a fragment
  shader (inverse projection plus HEALPix pixel lookup).
- Five projections (Mollweide, Plate Carree, Orthographic, Azimuthal
  equidistant, Gnomonic), all with north up and longitude increasing to the left. The
  disk projections are drawn as true circles, and the cursor readout,
  graticule labels, zoom-to-region and the minimap follow the projection.
- Support for both RING and NESTED pixel ordering, selectable from
  View > Pixel Ordering.
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
- Custom colormaps from CSV (`r,g,b` or `#rrggbb` per line) or JSON (a name and
  a list of colors, optionally with positions) files. Load them from
  View > Custom Colormaps > Load Colormap..., or put them in the `colormaps/`
  folder of the app's config directory. They are stored as rows of a 256-entry
  lookup texture, appear after the built-in colormaps in the toolbar and in
  Settings, and work with `--colormap` (by name or file path). Up to 64.
- The default colormap is saved by name, so custom colormaps can be the
  default.
- Reverse colormap option.
- Adjustable color range (min/max) with a "Reset Range" button that restores
  the range of the data.
- Linear and logarithmic color scales. If the data range includes values <= 0,
  the log scale uses a log-style stretch so it stays defined.
- Configurable background color (outside the ellipse) and unseen-pixel color.

### Navigation
- Zoom (1x to 50x) and pan, always zooming toward the cursor.
- Interactive sky rotation: Ctrl + left-drag or middle-drag in any projection
  (plain left-drag in the Orthographic and Azimuthal views) grabs the sky
  point under the cursor and keeps it there. Double-click or Reset View
  restores the orientation. View > Set Rotation... has Center longitude /
  Center latitude / Roll boxes that apply live and can be typed into, and
  View > Reset Rotation restores the orientation. The cursor readout and
  graticule labels follow the rotation.
- Perspective: every projection can be drawn as the sky seen from the inside
  (longitude increasing to the left, the default) or from the outside like a
  globe (the mirror image). Switch it in View > Perspective, set the default in
  Settings > Appearance, or use `--view inside|outside`. The cursor readout,
  dragging, zoom to region, minimap and graticule labels follow it.
- Orthographic globe animation (Settings > Appearance, on by default): a drag
  that ends while the globe is still moving lets it keep spinning and slow down
  with friction (any click stops it), and Reset View, Reset Rotation and
  double-click glide back to the default orientation instead of jumping.
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
- A Masks menu: Add Mask..., Clear All, and a submenu per mask with Enabled,
  Invert and Remove.
- Per-mask Invert inverts a mask (1 − m), for files that store 1 for
  "masked" instead of "keep". Like enabling, it only changes a uniform.
- Enabled masks are combined (product) on the GPU. Toggling a mask only
  changes a uniform and does not re-upload any data.
- Masks are stored as 8-bit values and packed four to an RGBA8 texture; up to
  16 masks can be loaded.
- "Hide" mode (Masks > Mode) paints masked pixels in the unseen color; "Dim"
  mode fades them with an adjustable strength (Masks > Dim Strength...).
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
- A toolbar with the frequently used controls: HDU, Column, Projection and
  Colormap. Everything else is in the menus: View (pixel ordering, colors,
  color scale, reverse colormap, color bar range, graticule, rotation), Masks
  and Tools.
- Toolbar and menu controls follow the current tab when you switch tabs.
- Clicking the map takes keyboard focus from the toolbar, so the shortcuts
  keep working.
- Statistics and Power Spectrum are dockable panels (Tools > Analysis): dock
  them to any edge, float them, or close them, and the layout is remembered.
  They follow the current tab. The power spectrum keeps a result per map and
  only recomputes when you press Compute / Recompute, and warns when the map's
  HDU, column or masks changed since.
- Tools > Analysis > Statistics shows nside, npix, minimum, maximum, average,
  RMS, standard deviation and the sky fraction (f_sky). The statistics are
  computed once on load and whenever masks change, so switching tabs with
  large maps stays fast. They only include pixels kept by the enabled masks.
- View menu toggles for the minimap and the color bar.
- Full-width status bar showing the file name, file size, zoom level, and the
  cursor's longitude/latitude, pixel index and pixel value.
- File > Open Recent lists the last 10 opened files (newest first, with a
  "Clear Recent Files" entry). The list is saved in
  `recent_files.txt` in the app data directory
  (`~/.local/share/hpvis/` on Linux).
- Open files from the File menu, or from the command line (`hpvis
  file.fits ...`). Command-line options override the saved defaults: `--hdu`,
  `--column`, `--colormap`, `--reverse`, `--scale`, `--min`, `--max`,
  `--projection`, `--graticule` and `--mask`. `--export FILE` saves the view as
  an image and exits, and `--info` / `--stats` print the HDUs and columns or
  the map statistics without opening a window.
- Files are opened with `fitshandle`: every HDU that holds a HEALPix map (NSIDE and
  ORDERING keywords, columns of 12*nside^2 values) is listed in the toolbar (HDU and Column), and switching either one
  reloads that map in the background. The first map HDU / column is used
  when a file is opened.
- File > FITS Header... opens a dialog with the FITS header cards
  (keyword, value, comment) of any HDU in the current file, with a filter box
  and a Copy button.
- Settings dialog (Tools > Settings) with General, Appearance, Overlays and
  About pages, grouped into sections, and a search box. It stores, with
  `QSettings`:
  - General: the number of recent files, whether file dialogs start in the
    last used folder, and toolbar / status bar visibility.
  - Appearance: the default projection, colormap, reverse, scale, background
    and unseen colors, and the orthographic shadow and limb darkening.
  - Overlays: color bar, minimap, graticule (visibility, spacing, color) and
    the default mask mode and dim strength.

  Changes also apply to maps that are already open.
- Maps and masks are read on background threads, so the window stays
  responsive while large files load (the status bar shows "Loading...").
  Several files opened together load in parallel.
- Map statistics (range, f_sky, average, RMS, standard deviation) and the mask
  weight conversion run on all CPU cores.
- Error dialog when a map cannot be loaded, including HEALPix library errors.

### Build and packaging
- CMake build with Qt 6, vendored cfitsio, and OpenMP linked. HEALPix C++ and
  libsharp are built from the vendored sources into the build tree, so no
  prebuilt libraries or absolute paths are kept in the repository.
- The project is named `hpvis`.
- Sample data in `samples/` (WMAP nside 32, Planck dust nside 2048, and two
  test masks).
