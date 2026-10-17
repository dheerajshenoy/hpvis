---
title: HPVIS
section: 1
header: User Commands
footer: hpvis
---

# NAME

hpvis - viewer and analysis tool for HEALPix maps

# SYNOPSIS

**hpvis** [*options*] [*FILE*...]

# DESCRIPTION

**hpvis** displays HEALPix maps, such as maps of the cosmic microwave
background, stored in FITS, NumPy (*.npy*) or NumPy archive (*.npz*) files. It
shows them in eleven projections, applies masks, computes map statistics,
histograms and power spectra (corrected for masks with the MASTER method),
and opens the results of operations on maps (arithmetic, resolution changes,
beam smoothing, derivatives, E and B maps) in new tabs. The view is saved as
an image, and the power spectrum and histogram plots as PNG, SVG or PDF.

Without **--output**, **--info**, **--stats**, **--help** or **--version** it
opens the graphical interface. Without files on the command line it reopens
the files of the last session.

# OPTIONS

**-h**, **--help**
:   Show help and exit.

**-v**, **--version**
:   Print version information and exit.

**-H**, **--hdu** *HDU*
:   HDU to visualize, 1-based (default: first map HDU). For a *.npz* file, the
    array number listed by **--info**; ignored for a *.npy* file.

**-c**, **--column** *COLUMN*
:   Column to visualize, by number or name such as *TT* (default: first
    column). For a 2-D *.npy* or *.npz* array, the row number.

**-m**, **--colormap** *COLORMAP*
:   Colormap, e.g. *viridis*, *inferno*, *jet*, the name of a custom
    colormap, or a colormap file (default: from the settings).

**--creverse**
:   Reverse the colormap.

**--cscale** *SCALE*
:   Colormap scale: *linear*, *log* or *asinh*.

**--cmin** *VALUE*, **--cmax** *VALUE*
:   Lower and upper end of the color range.

**-p**, **--projection** *PROJECTION*
:   One of *mollweide*, *equirectangular*, *orthographic*,
    *azimuthal_equidistant*, *gnomonic*, *mercator*, *sinusoidal*,
    *lambert_cylindrical*, *gall_peters*, *stereographic*, *hammer*.

**--orientation** *ORIENTATION*
:   Frame to show the sky in: *galactic*, *ecliptic* or *equatorial* (north
    stays up), or *free* (the map's own frame, default).

**--view** *VIEW*
:   *inside* (the sky, longitude increasing to the left, default) or
    *outside* (like a globe, mirrored).

**-g**, **--graticule**
:   Show the coordinate grid.

**--pixel-grid** *NSIDE*
:   Overlay the boundaries of the HEALPix pixels of this nside (rounded to a
    power of two).

**--mask** *FILE*
:   Mask file to apply. May be given more than once.

**-o**, **--output** *FILENAME*
:   Save the view of the first file as an image and exit.

**-i**, **--info**
:   Print the map HDUs and columns of the files (for a *.npz*, the arrays and
    rows) and exit, without opening a window.

**--stats**
:   Print min, max, mean, rms and standard deviation of the files and exit,
    without opening a window. UNSEEN and NaN pixels are left out.

# FILES

*~/.config/hpvis/hpvis.conf*
:   User settings, recent files and the last session.

*~/.config/hpvis/colormaps/*
:   Custom colormaps (CSV or JSON files), loaded at startup.

# EXAMPLES

Open a map in the Mollweide projection with a grid:

    hpvis -p mollweide -g map.fits

Export a Galactic-frame view without opening a window:

    hpvis --orientation galactic -o out.png map.fits

Show a map with a mask applied:

    hpvis --mask mask.fits map.fits

List the HDUs and columns of a file, then show the third column of the
second HDU:

    hpvis --info map.fits
    hpvis --hdu 2 --column 3 map.fits

Print the statistics of a map:

    hpvis --stats map.fits

# SEE ALSO

The usage guide in the program (**Help > Usage**) and in *docs/USAGE.md*,
and the HEALPix home page, <https://healpix.sourceforge.io>.

# AUTHOR

Dheeraj Vittal Shenoy, <https://github.com/dheerajshenoy/hpvis>.

# LICENSE

GNU Affero General Public License, version 3.
