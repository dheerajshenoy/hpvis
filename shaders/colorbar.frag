#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

// The colorbar (gradient, ticks, labels) rendered to a QImage; premultiplied
// alpha, top row first
uniform sampler2D colorbarTex;

void main() {
    FragColor = texture(colorbarTex, vec2(TexCoord.x, 1.0 - TexCoord.y));
}
