#pragma once

#include "colormap.hpp"
#include "fitsinfo.hpp"
#include "healpix_map.h"
#include "openglwidget.hpp"
#include "pixelordering.hpp"

#include <QVBoxLayout>
#include <QWidget>
#include <fitshandle.h>

class View : public QWidget
{
    Q_OBJECT

public:
    View(QWidget *parent = nullptr);
    ~View();

    inline const QString &filePath() const
    {
        return m_filePath;
    }

    inline int hduIndex() const
    {
        return m_hduIndex;
    }

    inline void setFilePath(const QString &path)
    {
        m_filePath = path;
    }

    // Direct access to the renderer, for settings (colormap, masks, ...)
    inline OpenGLWidget *glWidget() const
    {
        return m_glWidget;
    }

    inline const Healpix_Map<float> *healpixMap() const
    {
        return m_glWidget->healpixMap();
    }

    void setHduIndex(int index);

    // A map that was computed rather than read from a file: shown like any
    // other (no file, HDU or column)
    void setComputedMap(Healpix_Map<float> &&map, const QString &title,
                        SkyFrame frame);
    bool isComputed() const
    {
        return m_computed;
    }
    // Name for the tab and status bar: the file name, or the computed map's
    QString displayName() const;

    // Writes the map to a new FITS file (HDU 2, one float column)
    bool saveMap(const QString &path, QString *error) const;

    // Asynchronous: reads the file in the background and emits loadFinished
    // hdu / column are 1-based; 0 picks the first map HDU / column
    // columnName (if not empty) selects the column by name and takes
    // precedence over column
    void loadMap(const QString &filename, int hdu = 0, int column = 0,
                 const QString &columnName = QString());
    // Same file, another table HDU / column (both 1-based). Asynchronous;
    // emits hduLoaded.
    void loadHdu(int hdu, int column);

    const FitsInfo &fitsInfo() const
    {
        return m_fitsInfo;
    }
    int hdu() const
    {
        return m_hdu;
    }
    int column() const
    {
        return m_column;
    }
    bool loading() const
    {
        return m_loading;
    }

private:
    void initConnections();
    void startLoad(int hdu, int column, const QString &columnName,
                   bool initial);

signals:
    void loadFinished(bool ok, const QString &error); // first load only
    void hduLoaded(bool ok, const QString &error);    // after loadHdu()
    // lonLat in degrees; value is NaN and index -1 when off the map
    void cursorPosChanged(QPointF lonLat, QPointF raDec, double value,
                          qlonglong index);
    void zoomChanged(float zoom);
    void masksChanged();
    void colormapRangeChanged(float min, float max);

private:
    fitshandle m_fitsHandle;
    OpenGLWidget *m_glWidget = nullptr;
    QString m_filePath;
    QString m_title; // of a computed map
    bool m_computed = false;
    FitsInfo m_fitsInfo;
    int m_hdu      = 0;
    int m_column   = 0;
    bool m_loading = false;
    int m_hduIndex = -1;
};
