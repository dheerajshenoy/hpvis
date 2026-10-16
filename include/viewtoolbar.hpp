#pragma once

#include "colormap.hpp"
#include "fitsinfo.hpp"
#include "projection.hpp"
#include "skyframe.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QToolBar>

// The frequently used view controls. Everything else lives in the menus.
class ViewToolBar : public QToolBar
{
    Q_OBJECT

public:
    explicit ViewToolBar(QWidget *parent = nullptr);

    void setColormap(Colormap colormap);
    void setProjection(Projection projection);
    void setOrientation(Orientation orientation);

    // Lists the file's table HDUs and their columns (for NumPy files: the
    // arrays of a .npz and their rows), selecting `hdu`/`column` (both
    // 1-based) without emitting fitsSelectionChanged
    void setFitsInfo(const FitsInfo &info, int hdu, int column);

signals:
    void fitsSelectionChanged(int hdu, int column);
    void projectionChanged(Projection projection);
    void orientationChanged(Orientation orientation);
    void colormapChanged(Colormap colormap);

private:
    void widenPopup(QComboBox *combo);
    void fillColumnCombo(int hdu, int column);

    QLabel *m_hduLabel            = nullptr;
    QLabel *m_columnLabel         = nullptr;
    QAction *m_hduLabelAction     = nullptr;
    QAction *m_hduComboAction     = nullptr;
    QComboBox *m_hduCombo         = nullptr;
    QComboBox *m_columnCombo      = nullptr;
    QComboBox *m_orientationCombo = nullptr;
    QComboBox *m_projectionCombo  = nullptr;
    QComboBox *m_colormapCombo    = nullptr;
    FitsInfo m_fitsInfo;
};
