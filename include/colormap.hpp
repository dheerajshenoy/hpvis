#pragma once

enum class Colormap
{
    GRAYSCALE = 0,
    INFERNO   = 1,
    MAGMA     = 2,
    PLASMA    = 3,
    VIRIDIS   = 4,
    CUBEHELIX = 5,
    JET       = 6,
    HSV       = 7,
    HOT       = 8,
    COOL      = 9,
    SPRING    = 10,
    SUMMER    = 11,
    AUTUMN    = 12,
    WINTER    = 13
};

enum class ColormapScale
{
    LINEAR = 0,
    LOG    = 1
};
