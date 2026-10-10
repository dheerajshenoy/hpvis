// --- Colormap Helper Functions ---

vec3 sampleGrayscale(float t) {
    return vec3(t);
}

// Viridis approximation (Cubic polynomial fit)
vec3 sampleViridis(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    c.r = clamp(0.267 + t * (0.004 + t * (1.277 - 0.119 * t)), 0.0, 1.0);
    c.g = clamp(0.004 + t * (0.974 + t * (0.245 - 0.373 * t)), 0.0, 1.0);
    c.b = clamp(0.329 + t * (1.171 + t * (-1.164 + 0.560 * t)), 0.0, 1.0);
    return c;
}

// Inferno approximation
vec3 sampleInferno(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    c.r = clamp(t * (1.322 + t * (0.742 - 1.079 * t)), 0.0, 1.0);
    c.g = clamp(t * (0.063 + t * (1.614 - 0.710 * t)), 0.0, 1.0);
    c.b = clamp(0.145 + t * (2.197 + t * (-4.237 + 2.052 * t)), 0.0, 1.0);
    return c;
}

// Magma approximation
vec3 sampleMagma(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    c.r = clamp(t * (1.096 + t * (0.757 - 0.866 * t)), 0.0, 1.0);
    c.g = clamp(t * (0.054 + t * (1.621 - 0.771 * t)), 0.0, 1.0);
    c.b = clamp(0.252 + t * (2.298 + t * (-4.789 + 2.457 * t)), 0.0, 1.0);
    return c;
}

// Plasma approximation
vec3 samplePlasma(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    c.r = clamp(0.058 + t * (1.479 + t * (-0.722 + 0.201 * t)), 0.0, 1.0);
    c.g = clamp(0.014 + t * (0.421 + t * (1.574 - 1.011 * t)), 0.0, 1.0);
    c.b = clamp(0.536 + t * (1.439 + t * (-2.673 + 1.109 * t)), 0.0, 1.0);
    return c;
}

// Cubehelix (Perceptually uniform helix looping through color space)
vec3 sampleCubehelix(float t) {
    t = clamp(t, 0.0, 1.0);
    float ang = 2.0 * 3.14159265 * (14.0 / 3.0 * t + 0.5);
    float s = sin(ang);
    float c = cos(ang);
    float amp = t * (1.0 - t) * 0.5;
    float r = clamp(t + amp * (-0.14861 * c + 1.78277 * s), 0.0, 1.0);
    float g = clamp(t + amp * (-0.29227 * c - 0.90649 * s), 0.0, 1.0);
    float b = clamp(t + amp * ( 1.97294 * c), 0.0, 1.0);
    return vec3(r, g, b);
}

// Standard Jet / Rainbow
vec3 sampleJet(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    c.r = clamp(min(4.0 * t - 1.5, -4.0 * t + 4.5), 0.0, 1.0);
    c.g = clamp(min(4.0 * t - 0.5, -4.0 * t + 3.5), 0.0, 1.0);
    c.b = clamp(min(4.0 * t + 0.5, -4.0 * t + 2.5), 0.0, 1.0);
    return c;
}

// HSV / Rainbow wrap
vec3 sampleHsv(float t) {
    t = clamp(t, 0.0, 1.0);
    // Standard HSV to RGB conversion where H = t, S = 1.0, V = 1.0
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(vec3(t) + K.xyz) * 6.0 - K.www);
    return vec3(1.0) * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), 1.0);
}

// Hot (Black -> Red -> Yellow -> White)
vec3 sampleHot(float t) {
    t = clamp(t, 0.0, 1.0);
    float r = clamp(3.0 * t, 0.0, 1.0);
    float g = clamp(3.0 * t - 1.0, 0.0, 1.0);
    float b = clamp(3.0 * t - 2.0, 0.0, 1.0);
    return vec3(r, g, b);
}

// Cool (Cyan -> Magenta)
vec3 sampleCool(float t) {
    t = clamp(t, 0.0, 1.0);
    return vec3(t, 1.0 - t, 1.0);
}

// Spring (Magenta -> Yellow)
vec3 sampleSpring(float t) {
    t = clamp(t, 0.0, 1.0);
    return vec3(1.0, t, 1.0 - t);
}

// Summer (Green -> Yellow)
vec3 sampleSummer(float t) {
    t = clamp(t, 0.0, 1.0);
    return vec3(t, 0.5 + 0.5 * t, 0.4);
}

// Autumn (Red -> Yellow)
vec3 sampleAutumn(float t) {
    t = clamp(t, 0.0, 1.0);
    return vec3(1.0, t, 0.0);
}

// Winter (Blue -> Cyan -> Green)
vec3 sampleWinter(float t) {
    t = clamp(t, 0.0, 1.0);
    return vec3(0.0, t, 1.0 - 0.5 * t);
}

// Rainbow (matplotlib "rainbow": purple -> blue -> green -> yellow -> red)
vec3 sampleRainbow(float t) {
    t = clamp(t, 0.0, 1.0);
    return clamp(vec3(abs(2.0 * t - 0.5),
                      sin(t * 3.14159265),
                      cos(t * 1.57079633)), 0.0, 1.0);
}

// Master Switch Dispatcher
vec3 applyColormap(float t, int type) {
    switch(type) {
        case 0:  return sampleGrayscale(t);
        case 1:  return sampleInferno(t);
        case 2:  return sampleMagma(t);
        case 3:  return samplePlasma(t);
        case 4:  return sampleViridis(t);
        case 5:  return sampleCubehelix(t);
        case 6:  return sampleJet(t);
        case 7:  return sampleHsv(t);
        case 8:  return sampleHot(t);
        case 9:  return sampleCool(t);
        case 10: return sampleSpring(t);
        case 11: return sampleSummer(t);
        case 12: return sampleAutumn(t);
        case 13: return sampleWinter(t);
        case 14: return sampleRainbow(t);
        default: return sampleGrayscale(t);
    }
}
