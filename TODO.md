# TODO

# High Priority

- [ ] Plot axis labels close to the axes (not in the margins)
- [ ] Theory curve overlay add ability to change cosmo parameters realtime
- [x] Add plot reset view pushbutton
- [x] Add PDF and SVG export options
- [ ] Drag and drop map as images to other apps

# Medium Priority

- [ ] Point-source / peak finder (local maxima above N sigma, listed with positions; optional matched filter or wavelet)
- [ ] Real-space statistics: one-point PDF with skewness and kurtosis, two-point correlation function C(theta) from the C_l
- [ ] Mask builder (threshold, galactic cut, disc) with f_sky readout
- [ ] Multipole filtering: low-pass, high-pass, band-pass or notch in l (isolate the quadrupole or any l range)
- [ ] Monopole / dipole fit and removal outside a mask
- [ ] Wiener filtering and inpainting of cut-sky maps
- [ ] Needlets / wavelets and Minkowski functionals (non-Gaussianity checks)

# Low Priority

- [ ] Smooth/bilinear interpolation option
- [ ] Percentile clipping (1%/99%) and auto-range button
- [ ] Measure angular distance between two points
- [ ] Markers
- [ ] Text annotations
- [ ] Polarization display (Q/U → amplitude, vectors)
- [ ] Side-by-side / linked views (synced zoom, rotation, color range)
- [ ] Packaging (AppImage / Arch PKGBUILD) and README with screenshots
- [ ] Publication-quality export: vector output (SVG or PDF) with axes, a labeled color bar and a title.
- [ ] Session and state saving: restore open files, view, colormap and masks. A command-line --session would help scripting.
