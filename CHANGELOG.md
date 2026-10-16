# CHANGELOG

## 0.1.0

First development release.

### NumPy files
- `.npz` files open like FITS files. The toolbar shows an Array chooser (the
  FITS HDU one) with the arrays that hold a map, and a Row chooser (the
  column one) for 2-D arrays; for a `.npy` file only the Row chooser is shown.
  Masks, map operations, `--info`, `--stats`, `--hdu` and `--column` work with
  them too. Needs libzip.
- Arrays of a `.npz` over 4 GiB are not loaded until you confirm.

### Rendering
- Zoom out to 10%: the wheel, pinch and View > Zoom now go below 100%, which
  shrinks the map inside the window. View > Zoom levels set the zoom
  absolutely.
- Pixel numbers: View > Pixel Grid > Show Pixel Numbers labels the pixels of the
  grid with their index in the map's pixel ordering, where they are big enough
  on screen to hold the label.
- Add colorbar tick label antialiasing.
- Pixel grid overlay: View > Pixel Grid draws the boundaries of the HEALPix
  pixels of any nside (rounded to a power of two, 1 to 8192, independent of
  the map's own nside), with its own color, and `--pixel-grid NSIDE` on the
  command line. The lines fade out when the pixels get smaller than a few
  screen pixels.
- Overlay text (color bar labels, graticule labels) is rasterized with
  antialiasing, which the GL paint engine did not apply on a surface without
  multisampling.
- GPU Mollweide projection of HEALPix maps, computed per pixel in a fragment
  shader (inverse projection plus HEALPix pixel lookup).
- Eleven projections (Mollweide, Hammer, Equirectangular, Mercator, Sinusoidal,
  Lambert cylindrical, Gall-Peters, Orthographic, Azimuthal equidistant,
  Gnomonic, Stereographic), all with north up and longitude increasing to the left. The
  disk projections are drawn as true circles, and the cursor readout,
  graticule labels, zoom-to-region and the minimap follow the projection.
- Support for both RING and NESTED pixel ordering, selectable from
  View > Pixel Ordering.
- Longitude increases to the left, matching healpy.
- Pixels are looked up with integer math (`texelFetch`) so large `nside`
  values stay exact.
- The map is stored as a 12-layer 2D array texture, one `nside` x `nside` layer
  per HEALPix base face, for both RING and NESTED files. The shader finds the
  face and (x, y) of a pixel directly, so it needs no pixel numbers or
  ordering-specific code, and `nside` up to 8192 is accepted (limited by the
  GPU's maximum texture size). nside 8192 needs 3.2 GB of video memory; if the
  GPU cannot allocate it, a message is shown instead of loading the map.
  Masks use the same layout. The faces are spread over several textures of at
  most 1 GiB each, because some drivers (Mesa on Intel) refuse a single larger
  texture even when there is plenty of memory.
- Streaming for large maps: maps with an nside above a limit (Settings >
  General > Large maps, default 4096) keep only a coarse copy (at most nside
  2048, averaged from the full map) on the GPU, and the full-resolution tiles
  (512 x 512 texels) that are on screen are loaded on demand into a 256 MB
  atlas, found by the shader through a page table. Video memory use no longer
  depends on nside, so nside 8192 works on GPUs with little memory. Tiles are
  only loaded where a screen pixel covers less sky than a coarse texel, and
  load a few per frame, falling back to the coarse map until they arrive.
  Masks are shown at the coarse resolution while streaming.
- Pixels marked `UNSEEN` (and NaN/Inf) are drawn in a configurable color.
- Shaders are split into small files (`common`, `colormaps`, `healpix`,
  `projections`, `scale`, `masks`), assembled with an
  `#include` loader, and embedded in the binary as Qt resources.
- Shader compile and link errors are printed to the console.
- The map uploads to the GPU one face at a time, in parallel, and happens
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
- Switching projection fades from the old picture to the new one (300 ms); it
  can be turned off in Settings > Appearance > Animation.
- Zoom (1x to 1000x, i.e. up to 100000%) and pan, always zooming toward the
  cursor.
- Interactive sky rotation: Ctrl + left-drag or middle-drag in any projection
  (plain left-drag in the Orthographic and Azimuthal views) grabs the sky
  point under the cursor and keeps it there. Double-click or Reset View
  restores the orientation. View > Set Rotation... has Center longitude /
  Center latitude / Roll boxes that apply live and can be typed into, and
  View > Reset Rotation restores the orientation. The cursor readout and
  graticule labels follow the rotation.
- Orientation (View > Orientation, Settings > Appearance, `--orientation`):
  Free shows the map in its own frame, and dragging also rolls the view like a
  trackball. Galactic, Ecliptic and Equatorial (J2000) show the sky in that
  frame, converted on the GPU from the map's `COORDSYS` keyword (Galactic when
  absent), with the graticule in that frame and its north pole kept up while
  dragging. Switching resets the rotation. The status bar shows the cursor in
  the displayed frame (l, b / λ, β / RA, Dec) along with RA / Dec, which now
  also respects the map's frame instead of assuming Galactic. The roll box in
  Set Rotation is disabled when the north is locked.
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

### Region statistics
- Ctrl + Shift + drag (or View > Select Statistics Region) picks a rectangle
  in the current projection. The Statistics dock shows its pixel count, area,
  sky fraction, min, max, average, RMS and standard deviation, using the same
  validity and mask rules as the whole map. The region stays fixed on the sky
  and is outlined on the map; View > Clear Statistics Region removes it.
  The numbers are computed on a worker thread.
- The region can also be an ellipse, a polygon (click the corners; double
  click or Enter closes it, Esc cancels, right click removes the last corner)
  or a line (the great-circle arc between two dragged points, whose pixels are
  measured and whose length is shown instead of an area). View > Select
  Statistics Region is now a submenu with the four shapes.
- The histogram can plot the region next to the whole map (the "Region"
  option of the Statistics dock, remembered between sessions).

### Resolution
- Tools > Change Resolution lowers the nside of the current map and opens the
  result as a new tab. "Average the pixels" is healpy's ud_grade (optionally
  marking a pixel UNSEEN when any sub-pixel is). "Through the harmonics" does
  map2alm (iterated), drops the a_lm above the new resolution and runs
  alm2map, which filters out the power the new resolution cannot hold instead
  of aliasing it. Optionally the input map's pixel window function is divided
  out and the new nside's multiplied in; the windows are read from a FITS
  table (HEALPix's pixel_window_n*.fits), a .npy array or a text file.

### Masks
- Masks > Add Mask From Map Column... lists the columns of the open maps' files
  (and of any other FITS or .npy file you add) so a mask stored next to the map,
  or in another file, can be picked without leaving the viewer. Maps that only
  exist in memory (results of operations) can be used too.
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
- File explorer: a dock on the left (View > File Explorer) lists the folders and
  the map files (FITS, `.npy`, `.npz`) under a root folder, with a path bar, a
  parent-folder button and a "Show all files" option. Double-click or Enter
  opens a file in a new tab (or switches to its tab if it is already open);
  the right-click menu can set a folder as the root, open the containing
  folder and copy a path. The explorer follows the current tab.
- Drag and drop: map files dragged onto the window from the explorer or from a
  file manager open in new tabs; a dropped folder becomes the explorer's root.
- Custom keyboard shortcuts: Settings > Shortcuts lists every menu command and
  the viewer's region keys (finish polygon, cancel selection) and lets you bind
  any key or key chord to them. Binding a key that another command already
  uses takes it from that command, and says which one lost it.
- View > Zoom > Zoom In / Zoom Out and View > Pan > Left / Right / Up / Down
  commands, with no key by default, to bind in Settings > Shortcuts.
- Tabbed interface: open several maps at once, each with its own view and
  settings.
- A toolbar with the frequently used controls: HDU, Column, Projection,
  Orientation and Colormap. Everything else is in the menus: View (pixel ordering, colors,
  color scale, reverse colormap, color bar range, graticule, rotation), Masks
  and Tools.
- Toolbar and menu controls follow the current tab when you switch tabs.
- Clicking the map takes keyboard focus from the toolbar, so the shortcuts
  keep working.
- Statistics and Power Spectrum are dockable panels (Tools > Analysis): dock
  them to any edge, float them, or close them, and the layout is remembered.
  They follow the current tab. The power spectrum keeps a result per map and
  only recomputes when you press Compute / Recompute. While it runs the dock
  shows a progress bar with the current step and the elapsed time, and the
  button becomes Cancel (it stops after the current transform), and warns when the map's
  HDU, column or masks changed since.
- The power spectrum dock has Accuracy (Fast 0, Normal 1 or Accurate 3
  refinement rounds) and lmax (2 x nside, 3 x nside - 1 or nside) choices,
  remembered between runs. Fewer rounds and a lower lmax are much faster, and
  the dock shows what the current result used. The defaults are Normal and
  2 x nside.
- The power spectrum plot has a choice of y quantity (Dℓ, Cℓ or ℓ²Cℓ) and
  logarithmic ℓ and y axes (both remembered), a hover readout of ℓ and the
  value at the nearest point, wheel zoom around the cursor (Shift: ℓ only,
  Ctrl: y only), drag to pan and double-click to show everything. The Export
  menu saves the plot as a PNG image or all computed spectra (Cℓ and Dℓ for
  ℓ = 0..lmax, with nside, lmax and the refinement rounds in a header line) as
  CSV. The plot's background, foreground (text and axes) and line colors can
  be picked in the dock or in Settings > Appearance, and are remembered.
- Tools > Operate on Maps... combines maps pixel by pixel into a new map.
  Inputs are open maps and/or FITS files (each with its own HDU and column),
  named A, B, C, ...; the operation is a preset (add, subtract, multiply,
  divide, mean, absolute difference, maximum, minimum) or any expression such
  as `(A - B) / sqrt(C)` with + - * / ^, parentheses, abs, sqrt, log, log10,
  exp, sin, cos, tan, min, max, pow and pi. Maps with different nsides are
  brought to the highest, lowest or first nside, and orderings are matched.
  Unseen pixels, divisions by zero and other undefined values stay unseen in
  the result. The result opens as a new tab that works like any other map
  (statistics, power spectrum, masks, export), and File > Save Map As...
  writes any map, including a computed one, to a FITS file.
- The power spectrum dock can compare spectra. Its Compare menu overlays the
  spectra of other open maps (once computed there) and curves loaded from text
  files, such as a theory spectrum; the files have l in the first column and
  one column per spectrum (header names TT, EE, BB, TE, TB, EB, or C_l_TT /
  D_l_TT as in the CSV export; without a header the second column is TT), with
  values as C_l or D_l (asked). The plot gets a legend and the hover readout
  lists every curve at that multipole. The mode picks Overlay, Ratio to this
  map or Difference from this map, and all of it follows the chosen spectrum,
  y quantity and axis scales.
  "Common ℓ range" limits all curves to the highest multipole they all
  have (maps of different nside end at different multipoles). The CSV export
  includes the compared maps and curves too: their columns end in `@name`,
  and are empty where a curve has no value.
- Tools > Analysis > Statistics shows nside, npix, minimum, maximum, average,
  RMS, standard deviation and the sky fraction (f_sky). The statistics are
  computed once on load and whenever masks change, so switching tabs with
  large maps stays fast. They only include pixels kept by the enabled masks.
- The Statistics dock has a pixel value histogram of the current map. Choose
  the field (column) of the map, the number of bins, density or counts, a log
  y axis, whether the enabled masks are used, and whether to clip the outliers
  (0.1 % to 99.9 %). Compare overlays the histograms of other open maps on the
  same bins (the union of their ranges), or shows them as a ratio to, or a
  difference from, the current map. The plot has a legend, a hover readout of
  the bin and every curve's value, wheel zoom, drag pan and double-click to
  reset, uses the plot colors of the power spectrum plot, and can be exported
  as an image or as CSV (bins, counts and density of every curve). Fields are
  read from the file, so a 0 / 1 map shows its zeros; a computed map uses its
  values in memory.
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
  `--projection`, `--graticule`, `--pixel-grid` and `--mask`. `--export FILE` saves the view as
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
- Export Image opens a dialog first, asking for an optional title (drawn above
  the map), the format (PNG, JPEG, BMP), the width in device pixels, the JPEG
  quality and whether to keep a transparent background; the file name gets the
  extension of the chosen format when none is typed.

### Build and packaging
- A man page is written in `docs/MAN.md` and converted with pandoc during the
  build; it is installed to `share/man/man1` when pandoc is found.
- `INSTALLATION.md` lists the dependencies (CFITSIO, autotools, GSL, FFTW3,
  CBLAS, ...) with package commands for Arch, Debian/Ubuntu and Fedora, and
  the build and install steps.
- `cmake --install` installs the vendored HEALPix, libsharp and libnmt shared
  libraries to `lib/hpvis` and sets the binary's RPATH to find them. The
  bundled cfitsio is no longer built or installed, so it cannot overwrite the
  system library.
- CMake build with Qt 6, vendored cfitsio, and OpenMP linked. HEALPix C++ and
  libsharp are built from the vendored sources into the build tree, so no
  prebuilt libraries or absolute paths are kept in the repository. They are
  built with `-O3` to speed up the spherical harmonic transforms.
- The project is named `hpvis`.
- Sample data in `samples/` (WMAP nside 32, Planck dust nside 2048, and two
  test masks).
