#pragma once

#include "colormap.hpp"
#include "fitsinfo.hpp"
#include "projection.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QToolBar>

// The frequently used view controls. Everything else lives in the menus.
class ViewToolBar : public QToolBar
{
    Q_OBJECT

public:
    explicit ViewToolBar(QWidget *parent = nullptr);

    void setColormap(Colormap colormap);
    void setColormapReverse(bool reverse);
    void setColormapScale(ColormapScale scale);
    void setColormapRange(double min, double max);
    void setProjection(Projection projection);
    void setGraticule(bool enabled);

    // Lists the file's table HDUs and their columns, selecting `hdu`/`column`
    // (both 1-based) without emitting fitsSelectionChanged
    void setFitsInfo(const FitsInfo &info, int hdu, int column);

signals:
    void fitsSelectionChanged(int hdu, int column);
    void projectionChanged(Projection projection);
    void colormapChanged(Colormap colormap);
    void colormapReverseChanged(bool reversed);
    void colormapScaleChanged(ColormapScale scale);
    void colormapMinChanged(double min);
    void colormapMaxChanged(double max);
    void colormapRangeResetRequested();
    void graticuleToggled(bool enabled);

private:
    void fillColumnCombo(int hdu, int column);

    QComboBox *m_hduCombo             = nullptr;
    QComboBox *m_columnCombo          = nullptr;
    QComboBox *m_projectionCombo      = nullptr;
    QComboBox *m_colormapCombo        = nullptr;
    QCheckBox *m_reverseColormapCheck = nullptr;
    QComboBox *m_colormapScaleCombo   = nullptr;
    QDoubleSpinBox *m_colormapMinSpin = nullptr;
    QDoubleSpinBox *m_colormapMaxSpin = nullptr;
    QAction *m_graticuleAction        = nullptr;
    FitsInfo m_fitsInfo;
};
