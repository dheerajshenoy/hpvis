uniform sampler2D mapTexture;
uniform int nside;
uniform int npix;
uniform float vmin;
uniform float vmax;
uniform int pixel_ordering;
uniform int colormap;
uniform int colormap_scale; // 0 = linear, 1 = log
uniform bool invert_colormap;

#define PI 3.14159265358979323846
