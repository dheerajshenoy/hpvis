#pragma once

#include "colormap.hpp"
#include "pixelordering.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
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

    inline void setColormapReverse(bool reverse)
    {
        QSignalBlocker blocker(m_reverseColormapCheck);
        m_reverseColormapCheck->setChecked(reverse);
    }

    void setBackgroundColor(uint32_t color);
    void setUnseenColor(uint32_t color);
    void setPixelOrdering(PixelOrdering ordering);
    void setColormapScale(ColormapScale scale);
    void setColormapRange(double min, double max);

signals:
    void pixelOrderingChanged(PixelOrdering ordering);
    void colormapChanged(Colormap colormap);
    void colormapMinChanged(double min);
    void colormapMaxChanged(double max);
    void colormapScaleChanged(ColormapScale scale);
    void colormapRangeResetRequested();
    void colormapReverseChanged(bool state);
    void backgroundColorChanged(uint32_t color);
    void unseenColorChanged(uint32_t color);

private:
    void setupUI();
    void setupConnections();
    void onBackgroundColorButtonClicked();
    void onUnseenColorButtonClicked();
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
    QPushButton *m_bgColorButton      = nullptr;
    QPushButton *m_unseenColorButton  = nullptr;
    QColor m_bgColor                  = QColor(0, 0, 0);
    QColor m_unseenColor              = QColor(0, 0, 0);
};
