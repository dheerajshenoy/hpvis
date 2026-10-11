uniform vec2 viewport_size;
uniform vec2 view_scale; // window NDC -> map NDC (inset by the padding)
uniform sampler2D mapTexture;
uniform int nside;
uniform int npix;
uniform float vmin;
uniform float vmax;
uniform int pixel_ordering;
uniform int colormap;
uniform int colormap_scale; // 0 = linear, 1 = log
uniform bool invert_colormap;
// User colormaps: one 256-entry row each; ids >= 15 select row (id - 15)
uniform sampler2D colormapLut;
uniform int lut_rows;
uniform vec3 bg_color;
// Up to 16 masks, packed 4 per RGBA8 texture (one mask per channel)
uniform sampler2D maskTex0;
uniform sampler2D maskTex1;
uniform sampler2D maskTex2;
uniform sampler2D maskTex3;
uniform int mask_bits;       // bit i set = mask i enabled
uniform int mask_invert_bits; // bit i set = mask i inverted (1 - m)
uniform int mask_mode;       // 0 = hide, 1 = dim
uniform float mask_opacity;  // strength of "dim"
uniform float zoom;
uniform vec2 panOffset;
uniform vec3 unseen_color;
uniform bool globe_shadow;   // orthographic: soft shadow outside the globe
uniform mat3 frame_to_native; // displayed frame -> the frame the map is in
uniform bool view_outside;   // seen from the outside: mirror left-right
uniform bool limb_darkening; // orthographic: darken towards the limb

#define PI 3.14159265358979323846
