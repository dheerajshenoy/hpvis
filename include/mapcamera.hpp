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
        resetZoom();
        panOffset = QVector2D(0.0f, 0.0f);
    }

    void resetZoom()
    {
        zoom = 1.0f;
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

    // Fit the rectangle spanned by two screen points (NDC, y up) into the
    // window, keeping the window's aspect ratio. `viewScale` is the window
    // NDC -> map NDC factor. Zoom stays within
    // [1, maxZoom].
    void zoomToRect(QVector2D ndcA, QVector2D ndcB, QVector2D viewScale,
                    float maxZoom = 64.0f)
    {
        const QVector2D a = ndcA / zoom + panOffset; // projection plane
        const QVector2D b = ndcB / zoom + panOffset;
        const float w     = std::abs(a.x() - b.x());
        const float h     = std::abs(a.y() - b.y());
        if (w < 1e-6f || h < 1e-6f)
            return;

        // The window shows 2 * viewScale / zoom of the plane in each
        // direction
        zoom = std::clamp(
            std::min(2.0f * viewScale.x() / w, 2.0f * viewScale.y() / h), 1.0f,
            maxZoom);
        panOffset = (a + b) * 0.5f;
        clampPan();
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
