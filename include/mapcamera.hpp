#pragma once

#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QtGlobal>
#include <algorithm>

struct MapCamera
{
    float zoom          = 1.0f;
    QVector2D panOffset = QVector2D(0.0f, 0.0f);

    void reset()
    {
        zoom      = 1.0f;
        panOffset = QVector2D(0.0f, 0.0f);
    }

    // Clamp zoom levels to prevent inverse flipping or infinite scaling
    void adjustZoom(float factor, float minZoom = 0.1f, float maxZoom = 64.0f)
    {
        zoom = std::clamp(zoom * factor, minZoom, maxZoom);
        if (zoom <= minZoom)
        {
            panOffset
                = QVector2D(0.0f, 0.0f); // Reset pan when fully zoomed out
        }
    }

    // Keep the visible window inside the map. The window spans
    // panOffset +/- 1/zoom on the projection plane, which covers [-1, 1].
    void clampPan()
    {
        const float limit = std::max(0.0f, 1.0f - 1.0f / zoom);
        panOffset.setX(std::clamp(panOffset.x(), -limit, limit));
        panOffset.setY(std::clamp(panOffset.y(), -limit, limit));
    }

    // Zoom by `factor` keeping the point under `ndc` (cursor, [-1,1]^2, y up)
    // fixed on screen. The shader samples p = ndc / zoom + panOffset.
    void zoomAt(float factor, QVector2D ndc, float minZoom = 1.0f,
                float maxZoom = 50.0f)
    {
        const QVector2D anchor = ndc / zoom + panOffset;
        adjustZoom(factor, minZoom, maxZoom);
        if (zoom > minZoom)
        {
            panOffset = anchor - ndc / zoom;
            clampPan();
        }
    }

    // Pan by a screen-space delta in NDC units (content follows the cursor)
    void panBy(QVector2D ndcDelta)
    {
        if (zoom > 1.0f)
        {
            panOffset -= ndcDelta / zoom;
            clampPan();
        }
    }

    // Apply uniforms directly to the active shader program
    void uploadUniforms(QOpenGLShaderProgram &shader) const
    {
        shader.setUniformValue("zoom", zoom);
        shader.setUniformValue("panOffset", panOffset);
    }
};
