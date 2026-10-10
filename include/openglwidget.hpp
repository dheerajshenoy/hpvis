#pragma once

#include <QOpenGLFunctions>
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
    long m_nside            = 0;
    long m_npix             = 0;
    GLuint m_mapTexture     = 0;
    const float *m_map_data = nullptr;
};
