# Installation

## Dependencies

| Dependency | Needed for |
| --- | --- |
| C and C++20 compiler (GCC or Clang) | everything |
| CMake 3.22+, `make`, `pkg-config` | build system |
| Qt 6 (Core, Gui, Widgets, OpenGLWidgets, Concurrent, Graphs) | GUI |
| OpenMP | parallel code |
| **CFITSIO** | reading and writing FITS files (system package required) |
| **libzip** | reading `.npz` files (system package required) |
| autoconf, automake, libtool | building the vendored HEALPix, libsharp and NaMaster |
| GSL, FFTW3 (with OpenMP support), a CBLAS implementation | NaMaster power spectra (`WITH_NAMASTER=ON`) |
| `git` | fetching the submodules |
| `pandoc` (optional) | generating the man page |

HEALPix C++ and libsharp are built from the vendored sources; you do not need
to install them.

### Arch Linux

```sh
sudo pacman -S base-devel cmake git pkgconf qt6-base qt6-graphs cfitsio libzip \
    gsl fftw cblas-openblas autoconf automake libtool pandoc
```

### Debian / Ubuntu

```sh
sudo apt install build-essential cmake git pkg-config qt6-base-dev \
    qt6-graphs-dev libgl-dev libcfitsio-dev libzip-dev libgsl-dev libfftw3-dev \
    libopenblas-dev autoconf automake libtool pandoc
```

### Fedora

```sh
sudo dnf install gcc-c++ cmake git pkgconf-pkg-config qt6-qtbase-devel \
    qt6-qtgraphs-devel cfitsio-devel libzip-devel gsl-devel fftw-devel openblas-devel \
    autoconf automake libtool pandoc
```

Package names vary between distributions and releases; if a name is not
found, search for the library name (e.g. `qt6 graphs`).

## Build

```sh
git clone --recurse-submodules https://github.com/dheerajshenoy/hpvis
cd hpvis
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/hpvis
```

If you cloned without `--recurse-submodules`, run
`git submodule update --init --recursive`.

The first build is slow because it compiles HEALPix and NaMaster.

| Option | Default | Meaning |
| --- | --- | --- |
| `WITH_NAMASTER` | `ON` | Build NaMaster (power spectrum support) |
| `CMAKE_BUILD_TYPE` | `Release` | Build type |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Where to install |

## Install

```sh
sudo cmake --install build
```

This installs:

- `bin/hpvis`
- the vendored shared libraries (`libhealpix_cxx`, `libsharp`, `libnmt`) in
  `lib/hpvis/`, found by the binary through its RPATH
- `share/man/man1/hpvis.1` (only if `pandoc` was found at configure time)

To install somewhere else, for example without root:

```sh
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j
cmake --install build
```

Make sure `$HOME/.local/bin` is in your `PATH`.

## Troubleshooting

- **`cfitsio not found` at configure time**: install the CFITSIO development
  package. Never install a self-built CFITSIO over the system one.
- **`too many I/O drivers` / crash on opening a file**: two incompatible
  CFITSIO builds are in use. Reinstall the system package.
- **`error while loading shared libraries: libhealpix_cxx.so`**: reinstall
  with `cmake --install`; the libraries must be in `<prefix>/lib/hpvis`.
- **Qt `Graphs` not found**: you need Qt 6.8 or newer.
