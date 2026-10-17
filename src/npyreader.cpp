#include "npyreader.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <regex>
#include <stdexcept>

#include <zip.h>

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

namespace
{
// Fills the buffer completely or returns false
using Reader = std::function<bool(char *, size_t)>;

struct Header
{
    std::vector<size_t> shape;
    std::string descr;
    char kind     = 0;
    int size      = 0;
    bool fortran  = false;
};

Header
parseHeader(const Reader &read)
{
    char magic[8];
    if (!read(magic, 8) || std::memcmp(magic, "\x93NUMPY", 6) != 0)
        throw std::runtime_error("not a .npy file (bad magic)");

    const int major = static_cast<unsigned char>(magic[6]);
    uint32_t headerLen = 0;
    if (major == 1)
    {
        unsigned char b[2];
        if (!read(reinterpret_cast<char *>(b), 2))
            throw std::runtime_error("truncated .npy header");
        headerLen = b[0] | (b[1] << 8);
    }
    else if (major == 2 || major == 3)
    {
        unsigned char b[4];
        if (!read(reinterpret_cast<char *>(b), 4))
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
    if (!read(header.data(), headerLen))
        throw std::runtime_error("truncated .npy header");

    std::smatch m;
    if (!std::regex_search(header, m, std::regex("'descr'\\s*:\\s*'([^']*)'")))
        throw std::runtime_error("unsupported .npy dtype (structured array?)");

    Header h;
    h.descr = m[1];

    if (std::regex_search(header, m,
                          std::regex("'fortran_order'\\s*:\\s*(True|False)")))
        h.fortran = (m[1] == "True");

    if (!std::regex_search(header, m, std::regex("'shape'\\s*:\\s*\\(([^)]*)\\)")))
        throw std::runtime_error("missing shape in .npy header");
    {
        const std::string s = m[1];
        std::regex num("\\d+");
        for (auto it = std::sregex_iterator(s.begin(), s.end(), num);
             it != std::sregex_iterator(); ++it)
            h.shape.push_back(std::stoull(it->str()));
    }
    if (h.shape.empty() || h.shape.size() > 2)
        throw std::runtime_error("only 1-D and 2-D arrays are supported");

    // dtype: byte order char + kind + size, e.g. "<f4"
    const std::string &descr = h.descr;
    if (descr.size() < 3)
        throw std::runtime_error("unsupported .npy dtype '" + descr + "'");
    const char order = descr[0];
    h.kind           = descr[1];
    h.size           = std::atoi(descr.c_str() + 2);
    if (order == '>')
        throw std::runtime_error("big-endian .npy files are not supported");
    if (order != '<' && order != '|' && order != '=')
        throw std::runtime_error("unsupported .npy dtype '" + descr + "'");
    const bool intKind = h.kind == 'i' || h.kind == 'u';
    const bool ok      = (h.kind == 'f' && (h.size == 4 || h.size == 8))
                    || (intKind
                        && (h.size == 1 || h.size == 2 || h.size == 4
                            || h.size == 8));
    if (!ok)
        throw std::runtime_error("unsupported .npy dtype '" + descr + "'");
    return h;
}

// capped: refuse (NpzTooLargeError) an array of more than kNpzSizeCap bytes
NpyArray
readData(const Header &h, const Reader &read, bool capped)
{
    NpyArray out;
    out.shape = h.shape;

    size_t count = 1;
    for (size_t d : h.shape)
    {
        if (d != 0 && count > (size_t(1) << 56) / d)
            throw std::runtime_error("the array is too large");
        count *= d;
    }
    const uint64_t bytes = uint64_t(count) * uint64_t(h.size);
    if (capped && bytes > kNpzSizeCap)
        throw NpzTooLargeError(bytes);

    std::vector<char> raw(count * h.size);
    if (!read(raw.data(), raw.size()))
        throw std::runtime_error("file is smaller than its header says");

    std::vector<float> data(count);
    const char *p = raw.data();
    const char kind = h.kind;
    const int size  = h.size;
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
    else
        convert<uint64_t>(p, count, data.data());

    if (h.fortran && h.shape.size() == 2)
    {
        // Stored column-major: transpose into row-major
        const size_t r = h.shape[0], c = h.shape[1];
        out.data.resize(count);
        for (size_t i = 0; i < r; ++i)
            for (size_t j = 0; j < c; ++j)
                out.data[i * c + j] = data[j * r + i];
    }
    else
        out.data = std::move(data);
    return out;
}

// nside of a map with npix pixels, or 0 when npix is not 12*nside^2 with nside
// a power of 2
long
nsideOf(size_t npix)
{
    if (npix == 0)
        return 0;
    const long nside = std::lround(std::sqrt(double(npix) / 12.0));
    if (nside < 1 || size_t(12 * nside * nside) != npix
        || (nside & (nside - 1)) != 0)
        return 0;
    return nside;
}

int
rowsOf(const Header &h)
{
    return h.shape.size() == 2 ? static_cast<int>(h.shape[0]) : 1;
}

// --- .npz (a zip archive of .npy files) ---

struct ZipCloser
{
    void operator()(zip_t *z) const
    {
        zip_discard(z); // read-only: nothing to write back
    }
};
using ZipPtr = std::unique_ptr<zip_t, ZipCloser>;

struct ZipFileCloser
{
    void operator()(zip_file_t *f) const
    {
        zip_fclose(f);
    }
};
using ZipFilePtr = std::unique_ptr<zip_file_t, ZipFileCloser>;

ZipPtr
openZip(const std::string &path)
{
    int err     = 0;
    zip_t *zip = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (!zip)
    {
        zip_error_t e;
        zip_error_init_with_code(&e, err);
        const std::string why = zip_error_strerror(&e);
        zip_error_fini(&e);
        throw std::runtime_error("cannot open " + path + " as a .npz file ("
                                 + why + ")");
    }
    return ZipPtr(zip);
}

bool
isNpyMember(zip_t *zip, zip_uint64_t i, std::string *key = nullptr)
{
    const char *name = zip_get_name(zip, i, 0);
    if (!name)
        return false;
    const std::string n = name;
    if (n.size() <= 4 || n.compare(n.size() - 4, 4, ".npy") != 0)
        return false;
    if (key)
        *key = n.substr(0, n.size() - 4);
    return true;
}

// Reads the zip member at index i (0-based) with the given reader callback
template <typename Fn>
auto
withMember(zip_t *zip, zip_uint64_t i, Fn &&fn)
{
    ZipFilePtr file(zip_fopen_index(zip, i, 0));
    if (!file)
        throw std::runtime_error(std::string("cannot read ")
                                 + zip_strerror(zip));
    Reader read = [&file](char *buf, size_t n)
    {
        while (n > 0)
        {
            const zip_int64_t got = zip_fread(file.get(), buf, n);
            if (got <= 0)
                return false;
            buf += got;
            n -= size_t(got);
        }
        return true;
    };
    return fn(read);
}

std::mutex g_approvedMutex;
std::set<std::string> g_approved;

bool
isApproved(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_approvedMutex);
    return g_approved.count(path) > 0;
}

// array is 1-based as in the public API; 0 = first .npy member. Returns the
// 0-based zip index.
zip_uint64_t
memberIndex(zip_t *zip, int array)
{
    const zip_int64_t n = zip_get_num_entries(zip, 0);
    if (array > 0)
    {
        if (array > n || !isNpyMember(zip, zip_uint64_t(array - 1)))
            throw std::runtime_error("the file has no array number "
                                     + std::to_string(array));
        return zip_uint64_t(array - 1);
    }
    for (zip_int64_t i = 0; i < n; ++i)
        if (isNpyMember(zip, zip_uint64_t(i)))
            return zip_uint64_t(i);
    throw std::runtime_error("the .npz file holds no .npy array");
}

NpyArray
loadNpy(const std::string &path, int array)
{
    if (isNpzPath(path))
    {
        ZipPtr zip           = openZip(path);
        const zip_uint64_t i = memberIndex(zip.get(), array);
        const bool capped    = !isApproved(path);
        return withMember(zip.get(), i, [&](const Reader &read)
        {
            const Header h = parseHeader(read);
            return readData(h, read, capped);
        });
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path);
    Reader read = [&in](char *buf, size_t n)
    {
        return bool(in.read(buf, std::streamsize(n)));
    };
    const Header h = parseHeader(read);
    return readData(h, read, false);
}

Header
loadHeader(const std::string &path, int array)
{
    if (isNpzPath(path))
    {
        ZipPtr zip           = openZip(path);
        const zip_uint64_t i = memberIndex(zip.get(), array);
        return withMember(zip.get(), i, [](const Reader &read)
        { return parseHeader(read); });
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path);
    Reader read = [&in](char *buf, size_t n)
    {
        return bool(in.read(buf, std::streamsize(n)));
    };
    return parseHeader(read);
}

FitsHdu
hduOf(int index, const QString &name, const Header &h)
{
    FitsHdu hdu;
    hdu.index = index;
    hdu.name  = name;
    for (int r = 1; r <= rowsOf(h); ++r)
        hdu.columns.append({r, QString(), QString()});
    return hdu;
}
} // namespace

void
npzApproveLarge(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_approvedMutex);
    g_approved.insert(path);
}

NpyArray
readNpy(const std::string &path, int array)
{
    return loadNpy(path, array);
}

int
npyMapCount(const std::string &path, int array)
{
    return rowsOf(loadHeader(path, array));
}

FitsInfo
scanNumpy(const std::string &path)
{
    FitsInfo info;
    if (!isNpzPath(path))
    {
        info.kind     = MapFileKind::Npy;
        info.hduCount = 1;
        const Header h = loadHeader(path, 0);
        if (nsideOf(h.shape.back()))
            info.hdus.append(hduOf(1, QString(), h));
        return info;
    }

    info.kind = MapFileKind::Npz;
    ZipPtr zip = openZip(path);
    const zip_int64_t n = zip_get_num_entries(zip.get(), 0);
    for (zip_int64_t i = 0; i < n; ++i)
    {
        std::string key;
        if (!isNpyMember(zip.get(), zip_uint64_t(i), &key))
            continue;
        ++info.hduCount;
        try
        {
            const Header h = withMember(zip.get(), zip_uint64_t(i),
                                        [](const Reader &read)
            { return parseHeader(read); });
            if (nsideOf(h.shape.back()))
                info.hdus.append(
                    hduOf(int(i) + 1, QString::fromStdString(key), h));
        }
        catch (const std::runtime_error &)
        {
            // Not a map (strings, 3-D, ...): leave it out of the list
        }
    }
    return info;
}

std::shared_ptr<Healpix_Map<float>>
readNpyMap(const std::string &path, int column, int maxNside, int *columnCount,
           int array)
{
    // A .npz with no member given: the first one that holds a map
    if (isNpzPath(path) && array <= 0)
    {
        const FitsInfo info = scanNumpy(path);
        if (info.hdus.isEmpty())
            throw std::runtime_error(
                "no HEALPix map found in the .npz file (needs a 1-D or 2-D "
                "array with 12*nside^2 values)");
        array = info.hdus.first().index;
    }

    const NpyArray arr = readNpy(path, array);
    const size_t npix  = arr.shape.back();
    const size_t rows  = arr.shape.size() == 2 ? arr.shape[0] : 1;
    if (columnCount)
        *columnCount = static_cast<int>(rows);

    const long nside = nsideOf(npix);
    if (!nside)
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
