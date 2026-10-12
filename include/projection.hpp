#pragma once

enum class Projection
{
    MOLLWEIDE = 0,
    EQUIRECTANGULAR,
    ORTHOGRAPHIC,
    AZIMUTHAL_EQUIDISTANT,
    GNOMONIC,
    MERCATOR,
    SINUSOIDAL,
    LAMBERT_CYLINDRICAL,
    GALL_PETERS,
    STEREOGRAPHIC,
    HAMMER // keep last: the settings range check uses it
};
