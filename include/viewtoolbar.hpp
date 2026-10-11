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
    void setProjection(Projection projection);

    // Lists the file's table HDUs and their columns, selecting `hdu`/`column`
    // (both 1-based) without emitting fitsSelectionChanged
    void setFitsInfo(const FitsInfo &info, int hdu, int column);

signals:
    void fitsSelectionChanged(int hdu, int column);
    void projectionChanged(Projection projection);
    void colormapChanged(Colormap colormap);

private:
    void fillColumnCombo(int hdu, int column);

    QComboBox *m_hduCombo             = nullptr;
    QComboBox *m_columnCombo          = nullptr;
    QComboBox *m_projectionCombo      = nullptr;
    QComboBox *m_colormapCombo        = nullptr;
    FitsInfo m_fitsInfo;
};
