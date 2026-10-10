uniform sampler2D mapTexture;
uniform int nside;
uniform int npix;
uniform float vmin;
uniform float vmax;
uniform int pixel_ordering;
uniform int colormap;
uniform int colormap_scale; // 0 = linear, 1 = log
uniform bool invert_colormap;
uniform vec3 bg_color;
// Up to 16 masks, packed 4 per RGBA8 texture (one mask per channel)
uniform sampler2D maskTex0;
uniform sampler2D maskTex1;
uniform sampler2D maskTex2;
uniform sampler2D maskTex3;
uniform int mask_bits;       // bit i set = mask i enabled
uniform int mask_mode;       // 0 = hide, 1 = dim
uniform float mask_opacity;  // strength of "dim"
uniform float zoom;
uniform vec2 panOffset;
uniform vec3 unseen_color;

#define PI 3.14159265358979323846
