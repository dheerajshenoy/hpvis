#include "tilecache.hpp"

#include "parallel.hpp"

#include <QOpenGLContext>
#include <algorithm>
#include <healpix_base.h>
#include <iostream>

TileCache::~TileCache()
{
    // The GL objects need the context; the owner calls destroy() first
}

bool
TileCache::create(long nside, int capacity)
{
    destroy();
    initializeOpenGLFunctions();

    GLint maxLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    capacity = std::min(capacity, int(maxLayers));

    m_nside        = nside;
    m_tilesPerSide = int(nside / kTileSize);

    glGenTextures(1, &m_atlas);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas);
    for (; capacity >= 16; capacity /= 2)
    {
        glGetError(); // clear
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R32F, kTileSize, kTileSize,
                     capacity, 0, GL_RED, GL_FLOAT, nullptr);
        if (glGetError() == GL_NO_ERROR)
            break;
    }
    if (capacity < 16)
    {
        std::cerr << "[GL] no room for a tile atlas" << std::endl;
        destroy();
        return false;
    }
    m_capacity = capacity;
    for (GLenum pname : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER})
        glTexParameteri(GL_TEXTURE_2D_ARRAY, pname, GL_NEAREST);
    for (GLenum pname : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T})
        glTexParameteri(GL_TEXTURE_2D_ARRAY, pname, GL_CLAMP_TO_EDGE);

    // Page table: -1 everywhere (nothing resident)
    const int t = m_tilesPerSide;
    std::vector<float> none(size_t(t) * t * 12, -1.0f);
    glGenTextures(1, &m_pages);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_pages);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R32F, t, t, 12, 0, GL_RED,
                 GL_FLOAT, none.data());
    for (GLenum pname : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER})
        glTexParameteri(GL_TEXTURE_2D_ARRAY, pname, GL_NEAREST);
    for (GLenum pname : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T})
        glTexParameteri(GL_TEXTURE_2D_ARRAY, pname, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    m_slotKey.assign(size_t(m_capacity), -1);
    m_slotStamp.assign(size_t(m_capacity), 0);
    m_slotOfKey.clear();
    m_tile.resize(size_t(kTileSize) * kTileSize);
    m_frame = 0;
    return true;
}

void
TileCache::destroy()
{
    if (m_atlas || m_pages)
    {
        glDeleteTextures(1, &m_atlas);
        glDeleteTextures(1, &m_pages);
    }
    m_atlas = m_pages = 0;
    m_capacity        = 0;
    m_slotKey.clear();
    m_slotStamp.clear();
    m_slotOfKey.clear();
}

void
TileCache::upload(const Healpix_Map<float> &map, int key, int slot)
{
    const int t    = m_tilesPerSide;
    const int tx   = key % t;
    const int ty   = (key / t) % t;
    const int face = key / (t * t);

    const Healpix_Base base(int(m_nside), map.Scheme(), SET_NSIDE);
    const float *data = map.Map().begin();
    parallel::forRange(kTileSize, [&](long y0, long y1) {
        for (long y = y0; y < y1; ++y)
            for (int x = 0; x < kTileSize; ++x)
                m_tile[size_t(y) * kTileSize + x] = data[base.xyf2pix(
                    tx * kTileSize + x, ty * kTileSize + int(y), face)];
    });

    glBindTexture(GL_TEXTURE_2D_ARRAY, m_atlas);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot, kTileSize, kTileSize,
                    1, GL_RED, GL_FLOAT, m_tile.data());

    // Point the page table at the new tile (and away from the evicted one)
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_pages);
    auto setPage = [&](int k, float layer) {
        const int px = k % t, py = (k / t) % t, pf = k / (t * t);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, px, py, pf, 1, 1, 1, GL_RED,
                        GL_FLOAT, &layer);
    };
    if (m_slotKey[size_t(slot)] >= 0)
    {
        setPage(m_slotKey[size_t(slot)], -1.0f);
        m_slotOfKey.erase(m_slotKey[size_t(slot)]);
    }
    setPage(key, float(slot));
    m_slotKey[size_t(slot)] = key;
    m_slotOfKey[key]        = slot;
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

bool
TileCache::update(const Healpix_Map<float> &map, const std::vector<int> &wanted,
                  int budget)
{
    if (!m_atlas)
        return false;
    ++m_frame;

    // Keep what is still wanted
    for (int key : wanted)
        if (auto it = m_slotOfKey.find(key); it != m_slotOfKey.end())
            m_slotStamp[size_t(it->second)] = m_frame;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    bool more = false;
    for (int key : wanted)
    {
        if (m_slotOfKey.count(key))
            continue;
        if (budget <= 0)
        {
            more = true;
            break;
        }
        // A free slot, else the one wanted longest ago (never one wanted now)
        int slot = -1;
        for (int s = 0; s < m_capacity; ++s)
        {
            if (m_slotKey[size_t(s)] < 0)
            {
                slot = s;
                break;
            }
            if (m_slotStamp[size_t(s)] < m_frame
                && (slot < 0
                    || m_slotStamp[size_t(s)] < m_slotStamp[size_t(slot)]))
                slot = s;
        }
        if (slot < 0)
            break; // more wanted than fits; what is resident is the nearest
        upload(map, key, slot);
        m_slotStamp[size_t(slot)] = m_frame;
        --budget;
    }
    return more;
}
