#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

#include "common.glsl"
#include "colormaps.glsl"

// Renders into a 256x1 target: pixel i holds the colormap at i / 255
void main() {
    float t = (gl_FragCoord.x - 0.5) / 255.0;
    FragColor = vec4(applyColormap(t, colormap, invert_colormap), 1.0);
}
