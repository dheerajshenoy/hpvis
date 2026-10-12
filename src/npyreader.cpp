#include "npyreader.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace
{
template <typename T>
void
convert(const char *src, size_t n, float *dst)
{
    for (size_t i = 0; i < n; ++i)
    {
        T v;
        std::memcpy(&v, src + i * sizeof(T), sizeof(T));
        dst[i] = static_cast<float>(v);
    }
}
} // namespace

NpyArray
readNpy(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path);

    char magic[8];
    if (!in.read(magic, 8) || std::memcmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("not a .npy file (bad magic)");

    const int major = static_cast<unsigned char>(magic[6]);
    uint32_t headerLen = 0;
    if (major == 1)
    {
        unsigned char b[2];
        if (!in.read(reinterpret_cast<char *>(b), 2))
            throw std::runtime_error("truncated .npy header");
        headerLen = b[0] | (b[1] << 8);
    }
    else if (major == 2 || major == 3)
    {
        unsigned char b[4];
        if (!in.read(reinterpret_cast<char *>(b), 4))
            throw std::runtime_error("truncated .npy header");
        headerLen = b[0] | (b[1] << 8) | (b[2] << 16)
                    | (static_cast<uint32_t>(b[3]) << 24);
    }
    else
        throw std::runtime_error("unsupported .npy version "
                                 + std::to_string(major));
    if (headerLen == 0 || headerLen > (1u << 20))
        throw std::runtime_error("invalid .npy header length");

    std::string header(headerLen, '\0');
    if (!in.read(header.data(), headerLen))
        throw std::runtime_error("truncated .npy header");

    std::smatch m;
    if (!std::regex_search(header, m, std::regex("'descr'\\s*:\\s*'([^']*)'")))
        throw std::runtime_error("unsupported .npy dtype (structured array?)");
    const std::string descr = m[1];

    bool fortran = false;
    if (std::regex_search(header, m,
                          std::regex("'fortran_order'\\s*:\\s*(True|False)")))
        fortran = (m[1] == "True");

    if (!std::regex_search(header, m, std::regex("'shape'\\s*:\\s*\\(([^)]*)\\)")))
        throw std::runtime_error("missing shape in .npy header");

    NpyArray out;
    {
        const std::string s = m[1];
        std::regex num("\\d+");
        for (auto it = std::sregex_iterator(s.begin(), s.end(), num);
             it != std::sregex_iterator(); ++it)
            out.shape.push_back(std::stoull(it->str()));
    }
    if (out.shape.empty() || out.shape.size() > 2)
        throw std::runtime_error("only 1-D and 2-D arrays are supported");

    // dtype: byte order char + kind + size, e.g. "<f4"
    if (descr.size() < 3)
        throw std::runtime_error("unsupported .npy dtype '" + descr + "'");
    const char order = descr[0], kind = descr[1];
    const int size   = std::atoi(descr.c_str() + 2);
    if (order == '>')
        throw std::runtime_error("big-endian .npy files are not supported");
    if (order != '<' && order != '|' && order != '=')
        throw std::runtime_error("unsupported .npy dtype '" + descr + "'");

    size_t count = 1;
    for (size_t d : out.shape)
        count *= d;

    std::vector<char> raw(count * size);
    if (!in.read(raw.data(), raw.size()))
        throw std::runtime_error("file is smaller than its header says");

    std::vector<float> data(count);
    const char *p = raw.data();
    if (kind == 'f' && size == 4)
        convert<float>(p, count, data.data());
    else if (kind == 'f' && size == 8)
        convert<double>(p, count, data.data());
    else if (kind == 'i' && size == 1)
        convert<int8_t>(p, count, data.data());
    else if (kind == 'i' && size == 2)
        convert<int16_t>(p, count, data.data());
    else if (kind == 'i' && size == 4)
        convert<int32_t>(p, count, data.data());
    else if (kind == 'i' && size == 8)
        convert<int64_t>(p, count, data.data());
    else if (kind == 'u' && size == 1)
        convert<uint8_t>(p, count, data.data());
    else if (kind == 'u' && size == 2)
        convert<uint16_t>(p, count, data.data());
    else if (kind == 'u' && size == 4)
        convert<uint32_t>(p, count, data.data());
    else if (kind == 'u' && size == 8)
        convert<uint64_t>(p, count, data.data());
    else
        throw std::runtime_error("unsupported .npy dtype '" + descr + "'");

    if (fortran && out.shape.size() == 2)
    {
        // Stored column-major: transpose into row-major
        const size_t r = out.shape[0], c = out.shape[1];
        out.data.resize(count);
        for (size_t i = 0; i < r; ++i)
            for (size_t j = 0; j < c; ++j)
                out.data[i * c + j] = data[j * r + i];
    }
    else
        out.data = std::move(data);
    return out;
}

std::shared_ptr<Healpix_Map<float>>
readNpyMap(const std::string &path, int column, int maxNside, int *columnCount)
{
    const NpyArray arr = readNpy(path);
    const size_t npix  = arr.shape.back();
    const size_t rows  = arr.shape.size() == 2 ? arr.shape[0] : 1;
    if (columnCount)
        *columnCount = static_cast<int>(rows);

    const long nside = std::lround(std::sqrt(double(npix) / 12.0));
    if (npix == 0 || size_t(12 * nside * nside) != npix
        || (nside & (nside - 1)) != 0)
        throw std::runtime_error(
            "array length " + std::to_string(npix)
            + " is not a valid HEALPix size (12*nside^2, nside a power of 2)");
    if (nside > maxNside)
        throw std::runtime_error("nside " + std::to_string(nside)
                                 + " is currently not supported (maximum is "
                                 + std::to_string(maxNside) + ").");

    const size_t row = column <= 0 ? 0 : size_t(column - 1);
    if (row >= rows)
        throw std::runtime_error("column " + std::to_string(column)
                                 + " does not exist (the array has "
                                 + std::to_string(rows) + ")");

    auto map = std::make_shared<Healpix_Map<float>>();
    map->SetNside(nside, RING);
    const auto first = arr.data.begin() + row * npix;
    std::copy(first, first + npix, const_cast<float *>(map->Map().begin()));
    return map;
}
