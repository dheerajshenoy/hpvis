#pragma once

#include "fitsinfo.hpp"
#include "healpix_cxx/healpix_map.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

// A NumPy .npy array read into memory as floats (any supported dtype is
// converted). Data is C-ordered: shape {rows, npix} for 2-D arrays.
struct NpyArray
{
    std::vector<size_t> shape;
    std::vector<float> data;
};

// Reading a member of a .npz inflates it in memory. A member that would take
// more than this many bytes is refused with NpzTooLargeError (a compressed
// archive can be tiny and still hold gigabytes), unless the file was approved.
constexpr uint64_t kNpzSizeCap = 4ull << 30; // 4 GiB

struct NpzTooLargeError : std::runtime_error
{
    explicit NpzTooLargeError(uint64_t bytes)
        : std::runtime_error("the array needs " + std::to_string(bytes >> 20)
                             + " MiB of memory, more than the "
                             + std::to_string(kNpzSizeCap >> 30)
                             + " GiB limit for .npz files"),
          bytes(bytes)
    {
    }
    uint64_t bytes;
};

// Lets the .npz file at path be read whatever the size of its arrays (after
// the user agreed). Lasts for the session; thread-safe.
void
npzApproveLarge(const std::string &path);

// Supports little-endian f4, f8, i1/i2/i4/i8 and u1/u2/u4/u8, C or Fortran
// order, 1-D or 2-D. Throws std::runtime_error with a readable message.
//
// For a .npz, array is the 1-based position of the member in the archive
// (the "HDU" of scanNumpy()); 0 is the first one. Ignored for a .npy.
NpyArray
readNpy(const std::string &path, int array = 0);

// Number of maps in the array (rows of a 2-D array, 1 for a 1-D one); only
// reads the header
int
npyMapCount(const std::string &path, int array = 0);

inline bool
hasExtension(const std::string &path, const char *ext)
{
    const std::string e(ext);
    if (path.size() < e.size())
        return false;
    std::string tail = path.substr(path.size() - e.size());
    for (char &c : tail)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tail == e;
}

inline bool
isNpyPath(const std::string &path)
{
    return hasExtension(path, ".npy");
}

inline bool
isNpzPath(const std::string &path)
{
    return hasExtension(path, ".npz");
}

// .npy or .npz
inline bool
isNumpyPath(const std::string &path)
{
    return isNpyPath(path) || isNpzPath(path);
}

// The arrays of a NumPy file that can be shown as maps, in the shape the
// FITS chooser uses: one "HDU" per array (a .npy has just the one; a .npz
// one per member that holds 12*nside^2 values in 1-D or 2-D, indexed by the
// member's position in the archive and named by its key) and one "column" per
// row. Only reads headers. Throws std::runtime_error when the file cannot be
// read; hdus is empty when it holds no map.
FitsInfo
scanNumpy(const std::string &path);

// Reads a row of a .npy / .npz array as a RING-ordered HEALPix map. A 1-D
// array is one map; a 2-D array holds one map per row. column is 1-based (0 =
// first) like the FITS columns; *columnCount (if given) receives the number
// of maps. array picks the .npz member as in readNpy(), except that 0 is the
// first member holding a map. Throws when the length is not 12*nside^2,
// nside > maxNside or column is out of range.
std::shared_ptr<Healpix_Map<float>>
readNpyMap(const std::string &path, int column, int maxNside,
           int *columnCount = nullptr, int array = 0);
