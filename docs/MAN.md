---
title: HPVIS
section: 1
header: User Commands
footer: hpvis 0.1.0
---

# NAME

hpvis - viewer for HEALPix maps

# SYNOPSIS

**hpvis** [*options*] [*FILE*...]

# DESCRIPTION

**hpvis** displays HEALPix maps stored in FITS (or *.npy*) files in several
projections. It can apply masks, compute map statistics and power spectra, and
export the current view as an image. Without **--export**, **--info** or
**--stats** it opens the graphical interface.

# OPTIONS

**-H**, **--hdu** *HDU*
:   HDU to visualize, 1-based (default: first map HDU; ignored for *.npy*).

**-c**, **--column** *COLUMN*
:   Column to visualize, by number or name such as *TT* (default: first
    column; for a 2-D *.npy*, the row number).

**-m**, **--colormap** *COLORMAP*
:   Colormap, e.g. *viridis*, *inferno*, *jet* (default: from settings).

**-r**, **--reverse**
:   Reverse the colormap.

**-s**, **--scale** *SCALE*
:   Colormap scale: *linear* or *log*.

**--min** *VALUE*, **--max** *VALUE*
:   Lower and upper end of the color range.

**-p**, **--projection** *PROJECTION*
:   One of *mollweide*, *equirectangular*, *orthographic*,
    *azimuthal_equidistant*, *gnomonic*, *mercator*, *sinusoidal*,
    *lambert_cylindrical*, *gall_peters*, *stereographic*, *hammer*.

**-o**, **--orientation** *ORIENTATION*
:   Frame to show the sky in: *galactic*, *ecliptic* or *equatorial* (north
    stays up), or *free* (the map's own frame, default).

**--view** *VIEW*
:   *inside* (the sky, longitude to the left) or *outside* (like a globe,
    mirrored).

**-g**, **--graticule**
:   Show the coordinate grid.

**--mask** *FILE*
:   Mask file to apply. May be given more than once.

**-e**, **--export** *FILENAME*
:   Save the view of the first file as an image and exit.

**-i**, **--info**
:   Print the map HDUs and columns of the files and exit.

**--stats**
:   Print min, max, mean, rms and std of the files and exit.

**-h**, **--help**
:   Show help and exit.

# FILES

*~/.config/hpvis/*
:   User settings.

# EXAMPLES

Open a map in the Mollweide projection with a grid:

    hpvis -p mollweide -g map.fits

Export a galactic-frame view without opening a window:

    hpvis -o galactic -e out.png map.fits

# SEE ALSO

**pandoc**(1)
