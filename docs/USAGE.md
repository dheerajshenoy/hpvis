# hpvis usage guide

hpvis is a viewer and a small analysis tool for HEALPix maps, such as maps of
the cosmic microwave background (CMB). This guide explains every part of it.
Pick a section on the left; the search box finds sections that mention a word.

Menu items are written as **Menu > Item**, for example **Colormap > Color Bar**.

## 1. Getting started

### What a HEALPix map is

HEALPix divides the whole sphere into **12 × nside²** pixels that all have the
same area. The resolution is set by **nside** (a power of two: 1, 2, 4, ...).
Larger nside means smaller pixels: nside 1024 has about 12.6 million pixels of
about 3.4 arcminutes each. hpvis supports nside up to 8192.

The pixels can be numbered in two orders, **RING** (along rings of constant
latitude) and **NESTED** (a hierarchy where neighbours in the sky are
neighbours in the numbering). The map's data are the same; only the numbering
differs. hpvis reads both and can show either (**View > Pixel Ordering**).

A pixel with the special value **UNSEEN** (−1.6375 × 10³⁰) means "no data",
for example where the sky was masked. NaN and infinite values are treated the
same. hpvis paints these pixels in the *unseen color* and leaves them out of
all statistics.

### The window

- **Menu bar:** File, Edit, View, Colormap, Masks, Tools and Help. Related
  items are grouped in submenus (for example **View > Zoom**, **View >
  Rotation**, **View > Statistics Region**, **Tools > Maps**).
- **Toolbar:** the HDU and column of the file, the projection, the
  orientation and the colormap of the current map.
- **Tabs:** every open map has its own tab, with its own projection,
  colormap, range and zoom. Close a tab with **File > Close Tab** (`Ctrl+W`).
- **Status bar:** the file name, the zoom, and for the point under the cursor
  its longitude and latitude, its right ascension and declination (RA / Dec),
  its pixel number and its value.
- **Docks:** panels you can show, hide and drag around: **File Explorer**
  (**View > File Explorer**), **Statistics** and **Power Spectrum** (both in
  **Tools > Analysis**).

With no map open the window shows only the hpvis logo; open a file to begin.

## 2. Opening maps

### From the program

- **File > Open** (`Ctrl+O`) opens one or several map files.
- **File > Open Recent** (`Ctrl+Shift+O`) lists the last files you opened;
  **Clear Recent Files** empties the list. How many are remembered is set in
  **Tools > Settings > General**.
- **File Explorer** dock: browse folders and open maps with a double click or
  Enter.
- **Drag and drop** files onto the window.

### File types

- **FITS** (`.fits`, `.fit`, `.fts`): the usual HEALPix format. A file can
  hold several tables (HDUs) with several columns each.
- **NumPy** (`.npy`, `.npz`): an array is a map if it is 1-D with
  12 × nside² values, or 2-D with one such map per row. A `.npz` is an archive
  of arrays. NumPy files carry no header, so hpvis assumes the Galactic frame.

### Choosing what to show: HDU and column

The toolbar has two choosers. For a FITS file they are **HDU** (the table) and
**Column** (the map inside it, for example TEMPERATURE, Q_POLARISATION,
U_POLARISATION). For a `.npz` they become **Array** and **Row**; for a `.npy`
only the Row chooser appears. Only entries that really hold a map are listed.

### The File Explorer dock

A small file browser. The **up** button goes to the parent folder; the path
box lets you type a folder; **Show all files** also lists files that are not
maps. Right-click an entry for **Open**, **Set As Root**, **Open Containing
Folder** and **Copy Path**.

### Sessions

When you close hpvis, the open files (with their HDU / column) and the current
tab are remembered. The next start without files on the command line reopens
them. Turn this off in **Tools > Settings > General**, or reopen the last
session on demand with **File > Reopen Last Session**. Maps that were computed
(see Tools) and per-map view settings are not part of the session.

## 3. Moving around the map

### With the mouse

| Action | Input |
| --- | --- |
| Zoom | Mouse wheel, pinch, or Ctrl + scroll (toward the cursor) |
| Pan | Left-drag, or two-finger scroll |
| Rotate the sky | Middle-drag. In the Orthographic, Azimuthal equidistant, Lambert azimuthal, Gnomonic and Stereographic projections plain left-drag rotates |
| Center on a point | Right-click: the point glides to the center of the view along the shortest path (see **View > Rotation**) |
| Zoom to a region | Shift + left-drag a rectangle |
| Statistics of a region | Ctrl + Shift + left-drag a rectangle |
| Reset the view | Double-click, or `0` |

### With the menus

- **View > Zoom:** fixed levels from 10 % to 51200 %, **Zoom In**, **Zoom
  Out**, **Zoom To Region** (then drag a rectangle) and **Reset Zoom**.
- **View > Pan:** Left, Right, Up and Down. They have no keys by default;
  assign some in **Tools > Settings > Shortcuts**.
- **View > Rotation > Set Rotation...:** a small window with sliders and
  boxes for the longitude and latitude of the center and the roll, in degrees.
  It stays open next to the map, applies changes at once and follows the
  rotation you do with the mouse. **Reset** goes back.
- **View > Rotation > Center On:** **North Pole**, **South Pole** or
  **Equator** (longitude 0, latitude 0), in the frame being shown. The view
  glides there.
- **View > Rotation > Recenter on Right Click** (on by default): a right
  click on the map glides the point under it to the center of the view, along
  the great circle from the old center. When a frame is chosen the north pole
  stays up; in the Default (free) orientation **Lock Roll When Recentering**
  keeps the roll of the view instead of the one the path would give.
- **View > Reset View:** zoom and rotation together (`0`).
- **View > Minimap** (`M`): a small overview of the whole sky showing where
  the zoomed view is.

### Projections

Pick one in the toolbar. Fourteen are available:

- **Mollweide**, **Hammer**, **Aitoff**, **Sinusoidal**, **Lambert
  cylindrical**, **Gall-Peters**, **Werner**: whole-sky ovals, cylinders and
  the heart-shaped Werner map. Mollweide, Hammer, Sinusoidal, Werner and the
  cylindrical equal-area maps keep areas equal; Hammer and Aitoff have the
  same outline, and Aitoff compromises between area and angles.
- **Equirectangular**, **Mercator**: rectangles; Mercator stretches the poles.
- **Orthographic**, **Azimuthal equidistant**, **Lambert azimuthal**,
  **Gnomonic**, **Stereographic**: views of part of the sphere (Azimuthal
  equidistant and Lambert azimuthal show all of it in a disk), good for
  zooming into a region. Lambert azimuthal keeps areas equal. The globe can
  spin after you drag it (see Settings).

### Orientation (the coordinate frame)

**View > Orientation** (also in the toolbar):

- **Default:** the map's own frame; dragging also rolls the view.
- **Galactic, Ecliptic, Equatorial:** show the sky in that frame with north
  up. The map is converted on the fly from its own frame (read from the file's
  `COORDSYS`; NumPy files are assumed Galactic).

### Perspective

**View > Perspective** chooses how the sky is drawn:

- **Inside (sky):** as seen from the center; longitude grows to the *left*,
  like a sky chart (the healpy convention).
- **Outside (globe):** the mirror image, like a globe; longitude grows to the
  right.

### Pixel ordering

**View > Pixel Ordering > Nested / Ring** changes how hpvis interprets the
pixel numbers. It is already set from the file; change it only if a file
declares the wrong order (the map would look scrambled).

## 4. Color

### Colormaps

Choose in the toolbar. Built in: Grayscale, Inferno, Magma, Plasma, Viridis,
Cubehelix, Jet, HSV, Hot, Cool, Spring, Summer, Autumn, Winter and Rainbow.
**Viridis** is the default and is a good choice for most data.

**Custom colormaps** (**Colormap > Custom Colormaps**):

- **Load Colormap...** copies a CSV or JSON file into the colormaps folder.
- **Open Colormaps Folder** opens that folder.

A CSV file has one color per line, as `r,g,b` (values 0–1 or 0–255) or
`#rrggbb`; a header line and `#` comments are ignored. A JSON file has a
`name` and a list of `colors` (`"#rrggbb"` strings, `[r, g, b]` arrays, or
`{"position": 0–1, "color": ...}` for uneven spacing). Custom colormaps appear
after the built-in ones. You can have up to 64.

### Color scale

**Colormap > Color Bar** holds the scale:

- **Linear:** value maps directly to color.
- **Logarithmic:** brings out faint structure in data that spans orders of
  magnitude. If the range includes values at or below zero, a log-style
  stretch is used so the scale stays defined.
- **Asinh:** behaves like a log for large values and like a line near zero, so
  it also works for data with both signs. The **softening** slider sets the
  transition: left is more logarithmic, right is more linear.

### Range

The colormap covers a **range** of values; pixels outside it get the end
colors.

- By default the range is the minimum and maximum of the valid pixels (masked
  pixels are left out).
- **Colormap > Color Bar > Range...** lets you type a minimum and a maximum, with a
  **Reset** button.
- **Reset Range** goes back to the default.
- **Percentile Range...** sets the range from percentiles of the valid,
  unmasked pixels. With 1 it uses the 1st and 99th percentile, so a few
  extreme pixels (the Galactic plane, bright point sources) do not wash out the
  CMB fluctuations. This is the usual "auto-range" for CMB maps.

The command line has `--cmin` and `--cmax`.

### Color bar

**Colormap > Color Bar > Show Color Bar** (`C`) draws a bar with the value scale
over the map.

### Background and unseen colors

- **View > Colors > Background Color...:** the color outside the map (outside the oval,
  for example).
- **View > Colors > Unseen Pixel Color...:** the color of UNSEEN, NaN and masked pixels
  (in Hide mode).

## 5. Overlays

### Graticule

**View > Graticule** draws longitude and latitude lines: **Show Graticule**,
the **spacing** in degrees (1° to 90°), and the **color**. The labels follow
the chosen orientation.

### Pixel grid

**View > Pixel Grid** draws the outlines of the HEALPix pixels of any nside
(rounded to a power of two, 1–8192), independent of the map's own nside. This
shows how the sphere is divided. The lines fade out when the pixels become too
small to see. **Show Pixel Numbers** writes each pixel's index (in the map's
ordering) in the pixels big enough to hold it. The grid has its own color.
From the command line: `--pixel-grid NSIDE`.

## 6. Masks

A **mask** says which pixels to keep: usually 1 where the data are good and 0
where they are not (for example the Galactic plane). Masks need the same nside
as the map. A mask in the other pixel ordering is converted.

The **Masks** menu:

- **Add Mask...:** load one or several mask files (FITS or NumPy). You can add
  more at any time; up to 16 can be loaded.
- **Add Mask From Map Column...:** lists the columns of the open files, and
  lets you add other files, so a mask stored next to the map (or a result you
  computed) can be used without leaving the viewer.
- **Clear All:** removes all masks.
- A **submenu for each mask** with **Enabled** (switch it on or off),
  **Invert** (use 1 − mask, for files that store 1 for "masked") and
  **Remove**.
- **Mode:** **Hide** paints masked pixels in the unseen color; **Dim** fades
  them.
- **Dim Strength...:** how strong the fading is.

Enabled masks are multiplied together. They also affect the statistics, the
histogram, the color range and, when asked, the power spectrum. A map of a
different size drops the loaded masks.

## 7. Statistics

Open it with **Tools > Analysis > Statistics**.

### The table

For the whole map, and for a region if you select one, it shows: nside, number
of pixels, valid pixels, sky fraction, area, minimum, maximum, average, RMS and
standard deviation. UNSEEN, NaN and masked pixels are left out. **Copy table**
copies the table as text.

### Selecting a region

- **Ctrl + Shift + drag** a rectangle on the map, or
- **View > Statistics Region** and choose a shape: **Rectangle**,
  **Ellipse**, **Polygon** (click the corners; double-click or Enter closes it;
  Esc cancels; right-click removes the last corner) or **Line** (a great-circle
  arc: drag between its two ends).

**View > Statistics Region > Clear Region** (or **Clear region** in the dock) removes
it. The keys that finish and cancel a polygon can be changed in the shortcuts.

### The histogram

The same dock draws the histogram of the pixel values:

- **Field:** which column of the map.
- **Bins:** how many.
- **Density:** divide by the number of pixels and the bin width, so histograms
  of different maps can be compared.
- **Log y:** logarithmic vertical axis.
- **Use masks:** leave out the pixels the masks remove.
- **Clip outliers:** use the range from the 0.1 % to the 99.9 % percentile.
- **Region:** also plot the pixels inside the selected region.
- **Compare:** add the histogram of another open map, as an **Overlay**, a
  **Ratio** to this map or a **Difference** from it.
- **Export:** the plot as an image (PNG, or SVG or PDF as vector graphics), or
  the histogram as CSV. See **Exporting plots** in section 10.

On the plot, the wheel zooms, dragging pans and double-click resets.

## 8. The power spectrum

Open it with **Tools > Analysis > Power Spectrum**. It shows how the
fluctuations of the map are distributed over angular scales. The multipole ℓ
is roughly 180° divided by the angle. The spectrum **C_ℓ** is the variance of
the harmonic coefficients a_ℓm at each ℓ. The CMB temperature spectrum has its
famous acoustic peaks around ℓ ≈ 200, 540 and 800.

### Computing

Press **Compute** (it becomes **Cancel** while running and **Recompute**
after). Each map keeps its own result and nothing is recomputed on its own.

- **Accuracy** (Fast, Normal, Accurate): how many refinement rounds follow the
  first transform (0, 1 or 3). HEALPix pixels are not an exact quadrature, so
  refinement improves the answer at the price of two more transforms per
  round.
- **lmax:** the highest multipole: 2 × nside (default), 3 × nside − 1 (the
  maximum) or nside. The cost grows roughly as lmax³. Above about 2 × nside the
  spectrum is mostly noise and aliasing.
- **Spectrum:** TT for a single map. When the HDU has T, Q and U columns it also
  gives EE, BB, TE, TB and EB (E and B are the gradient-like and curl-like
  parts of the polarization). Choose which one to plot.

UNSEEN and NaN pixels are taken as zero in the transform. Without the mask
correction below, masks are ignored and the dock warns about it.

### Correcting for a mask (MASTER)

A mask mixes the angular scales, so a naive spectrum of a cut-sky map is
biased. Tick **Correct for the mask (NaMaster)** to apply the enabled masks as
weights and correct the spectra with the MASTER method: the pseudo-C_ℓ of the
masked map is decoupled with the mode-coupling matrix of the mask and
averaged into **bandpowers**. The result is drawn at the effective multipole of
each band. The dock reports the sky fraction f_sky and the condition of the
coupling matrix.

- **Apodization:** a sharp mask edge leaks power between scales, so the edge
  can be smoothed. **C1** and **C2** use a cosine ramp of the given width;
  **Smooth** uses a Gaussian. The scale is in degrees. It is slow for large maps
  (refused above nside 1024).
- **Δℓ:** how many multipoles go into each bandpower. Wider bands are less noisy
  and the matrix is easier to invert.
- **Purify E / Purify B:** removes the leakage of E modes into B (and the
  reverse) caused by the mask. It needs Q and U, an apodized mask, more time
  and memory, and raises the noise of the purified mode.
- **Limits:** the correction works up to nside 2048. With a mask lmax is
  limited to 4096 (2048 with polarization).

### The plot

- **Quantity:** D_ℓ = ℓ(ℓ+1) C_ℓ / 2π (the usual plot), C_ℓ, or ℓ² C_ℓ.
- **Log ℓ** and **Log y:** logarithmic axes.
- Wheel zoom, drag to pan, double-click to reset; hover for a readout.
- The plot **colors** (background, foreground, line) can be changed with the
  buttons here or in Settings.
- **Export:** the plot as an image (PNG, or SVG or PDF as vector graphics), or
  all spectra as CSV (bandpowers at their effective ℓ when corrected). See
  **Exporting plots** in section 10.

### Comparing spectra: the Compare menu

- **Other open maps** whose spectrum is computed: shown together, as an
  **Overlay**, as the **Ratio to this map** or as the **Difference from this
  map**.
- **Theory:** the theoretical CMB spectrum from the settings (by default the
  Planck 2018 best fit shipped with hpvis, including TT, TE, EE and the
  lensing BB). Tick **Bin theory** to convolve it with the bandpower windows,
  which is what bandpowers should match. **Common ℓ range** limits all curves
  to the highest multipole they share.
- **Load a curve from a file:** a text file with ℓ in the first column and one
  column per spectrum (a header naming them, like `TT`, `EE`, ...). You are
  asked whether the values are C_ℓ or D_ℓ. **Remove loaded curves** clears
  them.
- **Cross-spectrum (TT) with...:** the cross-spectrum C_ℓ^AB of this map and
  another open map of the same nside, added as a curve. If both maps hold the
  same sky signal but independent noise (for example two half-mission splits),
  the noise cancels and no noise bias has to be subtracted: it is the classic
  null test. The masks of the current map weight both maps and the result is
  divided by the mean squared weight, which is accurate for ℓ well above the
  size of the mask but does not remove the mode coupling like MASTER.

## 9. Tools

All tools open their result as a **new tab**. A computed map has no FITS file
yet: use **File > Save Map As...** to keep it.

### Operate on Maps

**Tools > Maps > Operate on Maps...** combines maps pixel by pixel.

1. Add the inputs: **Add Open Map** (from a tab) or **Add File...**. Each gets
   a name (A, B, C, ...) and you can choose its HDU and column.
2. Choose an **Operation** (Add, Subtract A − B, Multiply, Divide A / B, Mean,
   Absolute difference, Maximum, Minimum), or type your own **Expression**.
3. Choose the **Resolution** of the result when the maps differ: highest
   nside, lowest nside or the first map's. The others are up- or downgraded.
4. Name the result and press **Compute & Open**.

An expression uses the input names with `+ - * / ^` and parentheses, and the
functions `abs`, `sqrt`, `log`, `log10`, `exp`, `sin`, `cos`, `tan`, `min(a,
b)`, `max(a, b)`, `pow(a, b)`, and the constant `pi`. Example: `(A - B) /
sqrt(C)`. A pixel is UNSEEN in the result when an input it uses is UNSEEN or
the expression has no finite value there (a division by zero, for example).

### Change Resolution

**Tools > Maps > Change Resolution...** lowers the nside of the current map.

- **Average the pixels:** the mean of the sub-pixels, like healpy's `ud_grade`.
  Optionally a pixel becomes UNSEEN when any of its sub-pixels is. Power above
  ℓ = 3 × nside_out − 1 is aliased into the result.
- **Through the harmonics:** transforms to a_ℓm, drops the coefficients above
  the new resolution and transforms back. This filters out the power the new
  resolution cannot hold instead of aliasing it. Optionally the *pixel window
  function* of the input is divided out and the output's multiplied in (read
  from a FITS table such as HEALPix's `pixel_window_n*.fits`, a `.npy` array or
  a text file).

### Beam Smoothing

**Tools > Maps > Beam Smoothing...** convolves the map with a beam (a telescope's
blurring), through the harmonics.

- **Gaussian:** give the **FWHM** (full width at half maximum) in arcminutes,
  degrees or arcseconds. A negative FWHM *deconvolves*.
- **Custom beam window file:** the window function b_ℓ from ℓ = 0 (FITS table,
  `.npy` or text, one value per line).
- **Also smooth Q and U** (when the HDU has them): opens three maps, T, Q and U.
- **Iterations** refine the transform; **Analysis l max** limits the work.
- **Keep UNSEEN pixels UNSEEN** restores them in the result.

The dialog estimates the memory needed; above nside 1024 it can take long.

### Multipole Filter

**Tools > Maps > Multipole Filter...** keeps or removes a range of multipoles
(angular scales, roughly θ ≈ 180°/ℓ) through the harmonics: a_ℓm are multiplied
by a window f(ℓ). The result opens as a new tab.

- **Filter:** low-pass (keep ℓ ≤ ℓ max), high-pass (keep ℓ ≥ ℓ min; ℓ min = 2
  removes the monopole and dipole), band-pass or band-stop.
- **Edge:** a **Cosine** or **Gaussian** ramp of the given **width** (in
  multipoles, centred on the cut) or a **Sharp** step, which makes ringing in the
  map.
- The plot shows f(ℓ) from ℓ = 0 to the analysis ℓ max and follows the controls.
- **Also filter Q and U** (when the HDU has them): opens three maps; T, E and B
  get the same window.
- **Iterations**, **Analysis l max** and **Keep UNSEEN pixels UNSEEN** work as in
  Beam Smoothing. Power above the analysis ℓ max is dropped.

On a masked map the filter leaks power across the edge of the mask, since UNSEEN
pixels are taken as zero.

### Pixel window

Maps made of pixels are smoothed by the pixel (the *pixel window function*
w_ℓ). HEALPix gives two: w_T for temperature and w_P for polarization (E and B),
which differ. hpvis has them for nsides 2 to 8192 (the files of
`healpy.sphtfunc.pixwin`, bundled), picked by the map's nside.

- **Power spectrum dock > Pixel window** divides the spectra by w²: TT by w_T²,
  EE, BB and EB by w_P², TE and TB by w_T w_P. Bandpowers use the mean over their
  band. It applies to every map shown and to CSV exports, and needs no
  recomputing.
- **Theory pixel window** multiplies the theory curve by the same factors, to
  compare it with the pixelized map. Use one of the two, not both.
- **Tools > Maps > Generate Random Map > Apply the pixel window** (CMB) scales
  the a_ℓm by w_T for T and w_P for E and B, like `synfast(pixwin=True)`.

### Beam correction

Maps such as Planck's are smoothed to a beam (5 arcmin for SMICA), which lowers
the spectrum at high ℓ by B_ℓ². The power spectrum dock has a **Beam** row:

- **Beam** divides the spectra by B_ℓ², for the maps shown and for CSV exports,
  without recomputing. Give the **FWHM** of a Gaussian beam, or click **File...**
  for a beam window file (B_ℓ from ℓ = 0 as an amplitude, like HEALPix's
  `gaussian_beam`; a FITS table, `.npy` or text). Click the file's name to go
  back to the Gaussian.
- **Theory beam** multiplies the theory curve by B_ℓ² instead, to compare it with
  the smoothed map. Use one of the two, not both.
- Polarization (EE, BB, EB, TE, TB) uses the spin-2 Gaussian beam, as
  `smoothWithGauss` does. A FITS beam file with a POLARIZATION (or EE) column
  gives its own polarization beam; any other file is used for both.
- It combines with **Pixel window**: the total transfer function is B_ℓ w_ℓ.

### Derivatives and E/B Maps

**Tools > Maps > Derivatives and E/B Maps...** works through the spherical harmonics:

- **Laplacian (∇²T):** each a_ℓm is multiplied by −ℓ(ℓ+1). It highlights small
  scales, so noise grows quickly: lower the analysis l max to suppress it.
  Units are map units per steradian.
- **Gradient magnitude (|∇T|):** from the derivatives with respect to latitude
  and longitude. Units are map units per radian.
- **E and B maps:** splits the polarization (Q, U) of the map's HDU into its
  gradient-like **E** part and its curl-like **B** part, opened as two tabs.
  On a cut sky some E leaks into B near the edge of the mask. Needs T, Q and U
  columns in the file.

UNSEEN pixels are taken as zero in the transform and restored afterwards.

### Convert Units

**Tools > Maps > Convert Units...** multiplies the current map by the factor
between two units and opens the result as a new tab.

- **Units:** temperatures in K, mK or μK, either thermodynamic (**_CMB**: the
  temperature of a blackbody at 2.7255 K that gives the same change in
  brightness, the usual unit of CMB maps) or Rayleigh-Jeans (**_RJ**), and
  the brightness in **Jy/sr** and **MJy/sr**.
- **From** is read from the file's `TUNIT` keyword when it is there ("K", "uK",
  "muK", "K_CMB", "K_RJ", "MJy/sr", ...). A plain K, mK or μK is taken as a
  CMB temperature, as in the Planck files. Choose it by hand when it is
  missing or not recognized.
- **Frequency** is needed for anything that involves Rayleigh-Jeans or
  Jy/sr. The conversion is exact at that single frequency; it is not
  integrated over a detector bandpass, so it differs a little from the
  coefficients quoted for a real instrument band. Converting between K, mK
  and μK of the same kind needs no frequency.
- UNSEEN and NaN pixels stay as they are. The new tab remembers its unit.

### Generate Random Map

**Tools > Maps > Generate Random Map...** makes a map from scratch.

- **Gaussian noise** (mean and sigma) and **Uniform noise** (minimum and
  maximum): every pixel is an independent number.
- **CMB temperature**, or **temperature and polarization (T, Q, U)**: a
  Gaussian random sky drawn from the theory spectrum chosen in **Tools >
  Settings > Theory** (like HEALPix's `synfast`), in the **unit** you pick
  (μK, mK or K; the theory is in μK² and is scaled), optionally smoothed with
  a Gaussian **beam** (FWHM in arcminutes). It reaches l = 3 nside − 1 or the
  end of the spectrum, whichever is lower. Polarization opens three tabs
  (T, Q, U), and needs TT and EE columns in the theory file.
- **nside** from 1 to 8192, and the **seed**: leave it random, or set it to
  get the same map again (the seed is in the tab's name). The same seed
  gives the same map whatever the number of CPU cores.

CMB maps are Galactic, and their tabs know their unit (K_CMB family).

## 10. Saving and exporting

- **File > Export Image:** saves the view as an image (PNG, JPEG or BMP). Choose the
  title, size in pixels, quality, and, for PNG, whether the background stays
  transparent.
- **Edit > Copy Image** (`Ctrl+Shift+C`): copies the view to the clipboard.
- **File > Save Map As...:** writes the current map (including computed ones)
  as a FITS file.
- **File > FITS Header...:** shows the header of the current HDU (not for
  computed maps or NumPy files, which have none).
- **Command line:** `hpvis --output out.png map.fits` saves the view and exits.

### Exporting plots

The **Export** menus of the Power Spectrum and Statistics docks save the plot
as **PNG**, **SVG** or **PDF**. The format is the one of the file name's
extension (or the file type chosen in the dialog when you type none).

- PNG is a picture of the plot as it looks on screen.
- SVG and PDF are vector graphics: the plot is drawn again from its data, with
  the axes, ticks, grid, curves and legend, so it stays sharp at any size and
  the text stays selectable. They show what the plot shows now, including your
  zoom, the scales (linear or logarithmic) and the curves being compared.
- By default SVG and PDF keep the colors of the plot on screen. To get black
  on white (with the curve colors darkened so they read on paper), turn off
  **Keep the plot colors in SVG / PDF export** in **Tools > Settings >
  Appearance**.

## 11. Settings

**Tools > Settings** has a search box that finds pages and options by keyword.

- **General:** *Files* (how many recent files, whether dialogs open in the last
  folder, reopening the last session); *Large maps* (maps with a larger nside
  than the limit are *streamed*: a coarse copy stays on the graphics card and
  only the full-resolution tiles you look at are loaded, using only a few
  hundred MB); *Rendering* (HDR rendering with a 16-bit framebuffer avoids
  banding; turn it off if the display glitches on an old GPU; takes effect
  after a restart); *Interface* (show the toolbar and the status bar).
- **Appearance:** the defaults for new maps: projection, perspective,
  orientation, colormap, reverse, scale, background and unseen colors. For the
  orthographic globe: the shadow around the globe and limb darkening.
  *Animation*: the globe spins after dragging, and projections fade into each
  other. The colors of the power spectrum plot and histogram, and whether SVG
  and PDF exports keep them or use black on white.
- **Overlays:** whether the color bar and minimap show by default; the
  graticule (show, spacing, color); the masks (mode, dim strength).
- **Theory:** the theoretical spectrum for **Compare > Theory**: the bundled
  Planck 2018 best fit, or your own text file with ℓ, TT, TE, EE, BB in
  D_ℓ = ℓ(ℓ+1) C_ℓ / 2π in μK² (as CAMB and CLASS write it). The **default
  map unit** (μK, mK or K) is the unit of your maps when the file does not
  say: the unit in the FITS `TUNIT` keyword of the column being analysed
  takes precedence (when it is a CMB temperature), so the theory is scaled to
  the map you are looking at. The curve's name in the legend says when the
  unit came from the map.

  The Planck 2018 preset has editable **parameters**: base ΛCDM (H0, Ωbh²,
  Ωch², τ, ln(10¹⁰A_s), n_s), primordial (running, tensor-to-scalar ratio r,
  tensor tilt, or n_t = −r/8), neutrinos (N_eff and Σm_ν in one massive
  species), geometry and dark energy (Ωk, w0, wa) and thermal history (T_cmb,
  Y_He, or from BBN). With r > 0 the primordial tensor B modes are added to BB.
  The defaults are the Planck 2018 best fit and use the bundled spectrum. For other values the
  lensed TT, TE, EE and BB spectra are computed with the built-in CLASS when you
  save (a few seconds to a minute) and kept in the cache directory, so the same
  parameters are not computed twice. **Reset to Planck 2018** restores the
  defaults. The parameters also set the spectrum Random Map draws from.
- **Shortcuts:** see the next section.
- **About:** version, libraries and license.

## 12. Keyboard shortcuts

| Action | Default key |
| --- | --- |
| Open | `Ctrl+O` |
| Open Recent | `Ctrl+Shift+O` |
| Close Tab | `Ctrl+W` |
| Copy Image | `Ctrl+Shift+C` |
| Toggle minimap | `M` |
| Toggle color bar | `C` |
| Reset view | `0` |
| Finish a polygon region | `Enter` |
| Cancel a region selection | `Esc` |
| Usage guide | `F1` |

Almost every menu item can have a key. Open **Tools > Settings > Shortcuts**:
the table lists each command with its key and its default. Click a key box and
press the new combination to change it; clear it to remove the key. Use the
filter to find a command. If a key already belongs to another command, hpvis
says so and moves it to the new command. **Reset All to Defaults** restores
the original keys. Commands such as View > Pan have no key until you assign
one. A key belongs to the command, not to its place in the menus, so a
key you set stays with the command if the menus are reorganized in a later
version.

## 13. Command line

```sh
hpvis [options] [FILE...]
```

| Option | Description |
| --- | --- |
| `-H, --hdu N` | HDU to show (1-based, default: first map HDU); for a `.npz`, the array number listed by `--info` |
| `-c, --column N\|NAME` | Column to show, by number (1-based) or name such as `TT`; for a 2-D `.npy` / `.npz`, the row |
| `-m, --colormap NAME` | e.g. `viridis`, `inferno`, a custom colormap name, or a colormap file |
| `--creverse` | Reverse the colormap |
| `--cscale linear\|log\|asinh` | Color scale |
| `--cmin V`, `--cmax V` | Color range |
| `-p, --projection P` | `mollweide`, `equirectangular`, `orthographic`, `azimuthal_equidistant`, `gnomonic`, `mercator`, `sinusoidal`, `lambert_cylindrical`, `gall_peters`, `stereographic`, `hammer`, `aitoff`, `werner`, `lambert_azimuthal` |
| `--orientation O` | `free` (default), `galactic`, `ecliptic` or `equatorial` |
| `--view inside\|outside` | Perspective: the sky from the inside (default) or like a globe |
| `-g, --graticule` | Show the coordinate grid |
| `--pixel-grid NSIDE` | Overlay the pixel boundaries of this nside |
| `--mask FILE` | Apply a mask (repeatable) |
| `-o, --output FILE` | Save the view as an image and exit |
| `-i, --info` | List the HDUs and columns (for a `.npz`, arrays and rows) and exit |
| `--stats` | Print min, max, mean, rms and std and exit |
| `-v, --version` | Print the version |
| `-h, --help` | Print the help |

Examples:

```sh
hpvis map.fits
hpvis -p orthographic -m inferno -g map.fits
hpvis --mask mask.fits --output out.png map.fits
hpvis --info map.fits
hpvis --hdu 2 --column 3 maps.npz
hpvis --stats map.fits
```

`--info` and `--stats` print to the terminal and open no window.

## 14. Limits and troubleshooting

- **Maximum nside is 8192.** The map is kept on the graphics card as 32-bit
  floats, 4 bytes per pixel: nside 2048 needs 0.2 GB, 4096 needs 0.8 GB and
  8192 needs 3.2 GB of video memory. The full map is also held in memory
  (3.2 GB at nside 8192). Above the *stream* limit (Settings > General, default
  4096) only a coarse copy plus the visible tiles are on the card.
- **While streaming,** zooming in shows the coarse map for a moment until the
  tiles arrive, and masks are applied at the coarse resolution.
- **"Out of GPU memory"** message: lower the streaming limit in the settings.
- **The map looks scrambled:** the file's pixel ordering may be wrong. Try
  **View > Pixel Ordering**.
- **All pixels the same color:** check the color range and scale; a range
  that is too wide or too narrow for the data flattens the picture. Try
  **Percentile Range**.
- **Everything is the unseen color:** the map may be all UNSEEN, or the mask
  removes everything.
- **A mask is refused:** it must have the same nside as the map.
- **Banding or odd display on an old GPU:** turn off HDR rendering in the
  settings.
- **Power spectrum is slow:** use Fast accuracy, lower lmax, or downgrade the
  map first with **Tools > Maps > Change Resolution**. Mask correction and
  apodization are slow for large maps.
- **Floats:** maps are stored as 32-bit floats, even when a file holds 64-bit
  values (rounded when loaded). Transforms and sums are computed in double
  precision.

## 15. Glossary

- **HEALPix:** Hierarchical Equal Area isoLatitude Pixelization of the sphere.
- **nside:** the resolution parameter; a map has 12 × nside² pixels.
- **RING / NESTED:** the two ways to number the pixels.
- **UNSEEN:** the value (−1.6375 × 10³⁰) that marks a pixel with no data.
- **HDU:** one table inside a FITS file.
- **Mask:** a map of weights (0–1) that selects the part of the sky to use.
- **f_sky:** the fraction of the sky that is used.
- **Spherical harmonics (Y_ℓm), a_ℓm:** the "Fourier modes" of the sphere and
  the coefficients of a map in that basis. ℓ is the multipole (angular scale
  of about 180°/ℓ) and m the orientation.
- **C_ℓ:** the angular power spectrum: the variance of the a_ℓm at multipole ℓ.
  **D_ℓ** = ℓ(ℓ+1) C_ℓ / 2π is how it is usually plotted.
- **Pseudo-C_ℓ:** the spectrum of the masked map, which mixes scales.
- **MASTER:** the method that corrects the pseudo-C_ℓ for the mask with the
  mode-coupling matrix. **NaMaster** is the library that implements it.
- **Apodization:** smoothing the edge of a mask to reduce leakage.
- **Bandpower:** the spectrum averaged in a band of Δℓ multipoles.
- **E and B modes:** the two parts of the polarization pattern: E
  (gradient-like, even parity) and B (curl-like, odd parity). B-modes are a
  target of CMB polarization experiments.
- **Stokes parameters T, Q, U:** intensity and the two components of linear
  polarization.
- **Beam / FWHM:** the blurring of a telescope; its width at half maximum.
- **Pixel window:** the smoothing caused by averaging over a pixel.
- **Laplacian ∇²:** a measure of how much a map curves; on the sphere each
  harmonic mode has eigenvalue −ℓ(ℓ+1).
