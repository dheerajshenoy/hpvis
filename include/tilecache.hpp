#pragma once

#include "maptexformat.hpp"

#include <QOpenGLExtraFunctions>
#include <cstdint>
#include <healpix_map.h>
#include <unordered_map>
#include <vector>

// Streams full-resolution tiles of a HEALPix map to the GPU on demand, so the
// video memory in use does not depend on nside.
//
// Every base face is cut into kTileSize x kTileSize texel tiles. The resident
// tiles live in one array texture (the atlas, a layer per tile), and a small
// page table texture (tiles per side x tiles per side x 12 faces) tells the
// shader which layer holds a tile, or -1 when it is not resident (the shader
// then falls back to the always-resident coarse map). The atlas uses the
// map texture format (the page table is always R32F); both are float samplers
// like the other map textures.
class TileCache : protected QOpenGLExtraFunctions
{
  public:
    static constexpr int kTileSize = 512;

    // A tile: face and tile column / row inside the face
    static int
    key(int face, int tx, int ty, int tilesPerSide)
    {
        return (face * tilesPerSide + ty) * tilesPerSide + tx;
    }

    // True when tiles can be cut for this nside (a power of two above one
    // tile)
    static bool
    supports(long nside)
    {
        return nside > kTileSize && (nside & (nside - 1)) == 0;
    }

    TileCache() = default;
    ~TileCache();

    // Needs a current GL context. Tries `capacity` tiles (1 MB each at 32 bits), then
    // fewer; false when not even a few fit.
    // lo / hi: the map's value range, for the normalized formats.
    bool create(long nside, int capacity, MapTexFormat format, float lo,
                float hi);
    void destroy(); // needs a current GL context

    int tilesPerSide() const { return m_tilesPerSide; }
    int capacity() const { return m_capacity; }
    unsigned atlas() const { return m_atlas; }
    unsigned pages() const { return m_pages; }

    // Makes the `wanted` tiles (most important first) resident, uploading at
    // most `budget` of them, and evicting the least recently wanted ones.
    // Returns true when wanted tiles are still missing and could be loaded
    // by calling again.
    bool update(const Healpix_Map<float> &map, const std::vector<int> &wanted,
                int budget);

  private:
    void upload(const Healpix_Map<float> &map, int key, int slot);

    unsigned m_atlas      = 0;
    unsigned m_pages      = 0;
    long m_nside          = 0;
    int m_tilesPerSide    = 0;
    int m_capacity        = 0;
    uint64_t m_frame      = 0;
    std::vector<int> m_slotKey;        // tile in each slot, -1 = free
    std::vector<uint64_t> m_slotStamp; // frame the slot was last wanted
    std::unordered_map<int, int> m_slotOfKey;
    MapTexFormat m_format = MapTexFormat::Float32;
    float m_lo            = 0.0f;
    float m_hi            = 0.0f;
    std::vector<float> m_tile;         // upload staging
    std::vector<uint8_t> m_encoded;    // m_tile in the texture's format
};
