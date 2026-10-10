#pragma once

#include "colormap.hpp"
#include "pixelordering.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

class SidePanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit SidePanel(QWidget *parent = nullptr);

    inline PixelOrdering pixelOrdering()
    {
        return m_pixOrdCombo->currentIndex() == (int)PixelOrdering::NESTED
                   ? PixelOrdering::NESTED
                   : PixelOrdering::RING;
    }

    inline void setColormap(Colormap colormap)
    {
        m_colormapCombo->setCurrentIndex(static_cast<int>(colormap));
    }

    inline Colormap colormap()
    {
        return static_cast<Colormap>(m_colormapCombo->currentIndex());
    }

    void setPixelOrdering(PixelOrdering ordering);

    void setColormapScale(ColormapScale scale);

    inline void setColormapReverse(bool reverse)
    {
        QSignalBlocker blocker(m_reverseColormapCheck);
        m_reverseColormapCheck->setChecked(reverse);
    }

    // Update the min/max spinboxes without emitting change signals
    void setColormapRange(double min, double max);

signals:
    void pixelOrderingChanged(PixelOrdering ordering);
    void colormapChanged(Colormap colormap);
    void colormapMinChanged(double min);
    void colormapMaxChanged(double max);
    void colormapScaleChanged(ColormapScale scale);
    void colormapRangeResetRequested();
    void colormapReverseChanged(bool state);

private:
    void setupUI();
    void setupConnections();
    QWidget *pixelOrderingWidget();
    QWidget *colormapWidget();

private:
    QComboBox *m_pixOrdCombo          = nullptr;
    QComboBox *m_colormapCombo        = nullptr;
    QCheckBox *m_reverseColormapCheck = nullptr;
    QDoubleSpinBox *m_colormapMinSpin = nullptr;
    QDoubleSpinBox *m_colormapMaxSpin = nullptr;
    QComboBox *m_colormapScaleCombo   = nullptr;
    QPushButton *m_colormapResetBtn   = nullptr;
};
