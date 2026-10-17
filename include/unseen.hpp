#pragma once

#include <cmath>

// HEALPix UNSEEN sentinel (same value as Healpix_undef).
// NOTE: shaders/main.frag hardcodes an equivalent cutoff (-1.0e30); keep in sync.
inline constexpr float UNSEEN = -1.6375e30f;

// True for a usable pixel: finite and not UNSEEN. Compared against half of
// UNSEEN so values that drifted slightly (float round-trips, averaging) still
// count as UNSEEN.
inline bool
isSeen(float v)
{
    return std::isfinite(v) && v > UNSEEN * 0.5f;
}
