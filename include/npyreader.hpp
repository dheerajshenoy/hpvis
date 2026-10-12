#pragma once

#include "healpix_cxx/healpix_map.h"

#include <cstddef>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

// A NumPy .npy file read into memory as floats (any supported dtype is
// converted). Data is C-ordered: shape {rows, npix} for 2-D arrays.
struct NpyArray
{
    std::vector<size_t> shape;
    std::vector<float> data;
};

// Supports little-endian f4, f8, i1/i2/i4/i8 and u1/u2/u4/u8, C or Fortran
// order, 1-D or 2-D. Throws std::runtime_error with a readable message.
NpyArray
readNpy(const std::string &path);

inline bool
isNpyPath(const std::string &path)
{
    if (path.size() < 4)
        return false;
    std::string ext = path.substr(path.size() - 4);
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".npy";
}

// Reads a .npy file as a RING-ordered HEALPix map. A 1-D array is one map; a
// 2-D array holds one map per row. column is 1-based (0 = first) like the
// FITS columns; *columnCount (if given) receives the number of maps. Throws
// when the length is not 12*nside^2, nside > maxNside or column is out of
// range.
std::shared_ptr<Healpix_Map<float>>
readNpyMap(const std::string &path, int column, int maxNside,
           int *columnCount = nullptr);
