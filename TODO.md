# TODO

# High Priority

- [ ] Interactive Mask builder (threshold, galactic cut, disc) with f_sky readout

# Medium Priority

- [ ] Point-source / peak finder (local maxima above N sigma, listed with positions; optional matched filter or wavelet)
- [ ] Real-space statistics: one-point PDF with skewness and kurtosis, two-point correlation function C(theta) from the C_l
- [ ] More statistics in the Statistics dock: median, L-moments (L-scale, L-skewness, L-kurtosis), fixed percentiles (2.3, 15.9, 84.1, 97.7 %), and a PDF / CDF chart
- [ ] Data transforms: log, asinh, atan, tanh, power law and exponential with mu and sigma parameters, plus histogram equalization and normalization (the color scales now only change the colors, not the data)
- [ ] Partial-sky (indexed, INDXSCHM / OBJECT = PARTIAL) maps, and integer maps such as hit counts
- [ ] Monopole / dipole fit and removal outside a mask
- [ ] Wiener filtering and inpainting of cut-sky maps
- [ ] Needlets / wavelets and Minkowski functionals (non-Gaussianity checks)

# Low Priority

- [ ] Smooth/bilinear interpolation option
- [ ] Percentile clipping (1%/99%) and auto-range button
- [ ] Measure angular distance between two points
- [ ] Markers
- [ ] Text annotations
- [ ] Polarization display: choose Q, U, E, B or the amplitude P of a map as the data, and vector fields (with line integral convolution)
- [ ] Range modes: full, symmetric, positive only, negative only
- [ ] Custom gradient editor (colormap stops blended in a perceptual space such as okLab)
- [ ] Lighting effects: Lambert shading of the globe with a movable light
- [ ] More export formats: GIF, HEIF, 16-bit TIFF and OpenEXR (HDR); oversampling for large exports; size by width, height or fit; toggles for color bar, range and annotation
- [ ] Copy and paste the view settings (projection, viewpoint, colormap, scale, range) between maps
- [ ] Map list sidebar with thumbnails, as an alternative to the tabs
- [ ] Rendering settings: texture precision, antialiasing mode, proxy map for fast interaction, GPU choice, light / dark / system appearance
- [ ] False color: mix three maps into RGB (primaries, black / white points, gamma, decorrelation, gamut compression)
- [ ] Component separation (CMB, synchrotron, dust) from several frequency maps with an adjustable spectral model
- [ ] Side-by-side / linked views (synced zoom, rotation, color range)
