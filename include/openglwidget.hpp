#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <healpix_map.h>

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit OpenGLWidget(QWidget *parent = nullptr);
    ~OpenGLWidget();
    void setMap(const Healpix_Map<float> &map);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void uploadMapToGPU(const Healpix_Map<float> &map);
    void setupGeometry();

private:
    long m_nside         = 0;
    long m_npix          = 0;
    GLuint m_mapTexture  = 0;
    float m_vmin         = 0.0f;
    float m_vmax         = 1.0f;
    bool m_hasPendingMap = false;
    Healpix_Map<float> m_pendingMap;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer}; // VBO wrapper
    QOpenGLShaderProgram m_shaderProgram;
};
