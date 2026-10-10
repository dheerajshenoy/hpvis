// Map a data value to [0, 1] using the selected colormap scale.
float applyScale(float v) {
    if (colormap_scale == 1) {
        if (vmin > 0.0) {
            // Strictly positive range: true logarithmic scale
            float lv = log(max(v, vmin));
            return clamp((lv - log(vmin)) / (log(vmax) - log(vmin)), 0.0, 1.0);
        }
        // Range includes values <= 0 (e.g. CMB maps): log is undefined, so
        // use a log-style stretch of the normalized value, log(1 + k*u)
        float u = clamp((v - vmin) / (vmax - vmin), 0.0, 1.0);
        const float k = 1000.0;
        return log(1.0 + k * u) / log(1.0 + k);
    }
    return clamp((v - vmin) / (vmax - vmin), 0.0, 1.0);
}
