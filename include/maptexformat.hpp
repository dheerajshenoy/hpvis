#pragma once

#include "unseen.hpp"

#include <QFloat16>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// How the map values are stored in the GPU textures. Float32 is exact;
// the others trade precision for video memory and for support on older GPUs.
enum class MapTexFormat
{
    Float32 = 0, // R32F
    Float16 = 1, // R16F: ~3 digits, |v| < 65504 (larger values show as unseen)
    Norm16  = 2, // R16: 65534 levels between the map's min and max
    Norm8   = 3  // R8: 254 levels between the map's min and max
};

struct MapTexInfo
{
    GLenum internalFormat;
    GLenum format;
    GLenum type;
    int bytes; // per texel
};

inline MapTexInfo
mapTexInfo(MapTexFormat f)
{
    switch (f)
    {
    case MapTexFormat::Float16:
        return {GL_R16F, GL_RED, GL_HALF_FLOAT, 2};
    case MapTexFormat::Norm16:
        return {GL_R16, GL_RED, GL_UNSIGNED_SHORT, 2};
    case MapTexFormat::Norm8:
        return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
    default:
        return {GL_R32F, GL_RED, GL_FLOAT, 4};
    }
}

// Normalized formats code 0 as unseen and 1..levels-1 as lo..hi
inline int
mapTexLevels(MapTexFormat f)
{
    return f == MapTexFormat::Norm16 ? 65535 : f == MapTexFormat::Norm8 ? 255 : 0;
}

inline bool
mapTexNormalized(MapTexFormat f)
{
    return mapTexLevels(f) > 0;
}

// Converts n floats to the texture's texel type (little-endian bytes in out)
inline void
encodeMapTexels(const float *src, size_t n, MapTexFormat f, float lo, float hi,
                std::vector<uint8_t> &out)
{
    const MapTexInfo info = mapTexInfo(f);
    out.resize(n * size_t(info.bytes));
    if (f == MapTexFormat::Float32)
    {
        std::memcpy(out.data(), src, n * sizeof(float));
        return;
    }
    if (f == MapTexFormat::Float16)
    {
        auto *dst = reinterpret_cast<qfloat16 *>(out.data());
        for (size_t i = 0; i < n; ++i)
            dst[i] = isSeen(src[i]) ? qfloat16(src[i])
                                    : qfloat16(std::nanf(""));
        return;
    }
    const int levels  = mapTexLevels(f);
    const float span  = hi - lo;
    const float scale = span > 0.0f ? float(levels - 1) / span : 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        int code = 0;
        if (isSeen(src[i]))
            code = 1 + int(std::clamp((src[i] - lo) * scale + 0.5f, 0.0f,
                                      float(levels - 2)));
        if (f == MapTexFormat::Norm16)
        {
            const uint16_t c = uint16_t(code);
            std::memcpy(out.data() + i * 2, &c, 2);
        }
        else
            out[i] = uint8_t(code);
    }
}
