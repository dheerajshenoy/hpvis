#pragma once

#include "colormap.hpp"
#include "pixelordering.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
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

    inline float fsky() const
    {
        return m_fskyLabel->text().toFloat();
    }

    inline bool colorBarVisible() const
    {
        return m_colorBarCheck->isChecked();
    }

    inline void setColorBarVisible(bool visible)
    {
        QSignalBlocker blocker(m_colorBarCheck);
        m_colorBarCheck->setChecked(visible);
    }

    void setFSky(float fsky)
    {
        m_fskyLabel->setText(QString::number(fsky, 'f', 6));
    }

    void setBackgroundColor(uint32_t color);
    void setUnseenColor(uint32_t color);
    void setPixelOrdering(PixelOrdering ordering);
    void setColormapScale(ColormapScale scale);
    void setColormapRange(double min, double max);
    // Rebuilds the mask list (one checkbox row per mask) and syncs the mode
    // and strength controls, without emitting signals
    void setMasks(const QStringList &names, const QStringList &paths,
                  const QList<bool> &enabled, const QList<bool> &inverted,
                  int mode, float opacity);

    void setMapInfo(int nside, int npix, double avg, double rms,
                    double std);

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
    void maskFilesSelected(const QStringList &files);
    void maskClearRequested();
    void maskToggled(int index, bool enabled);
    void maskInvertToggled(int index, bool inverted);
    void maskRemoveRequested(int index);
    void maskModeChanged(int mode);
    void maskOpacityChanged(float opacity);
    void colorBarToggled(bool enabled);

private:
    void setupUI();
    void setupConnections();
    void onBackgroundColorButtonClicked();
    void onUnseenColorButtonClicked();
    void onLoadMaskButtonClicked();
    void updateDimRowVisibility();
    QWidget *pixelOrderingWidget();
    QWidget *colormapWidget();
    QWidget *overlaysWidget();
    QWidget *mapInfoWidget();

private:
    // Pixel ordering controls
    QComboBox *m_pixOrdCombo = nullptr;

    // Color & Colormap controls
    QComboBox *m_colormapCombo        = nullptr;
    QCheckBox *m_reverseColormapCheck = nullptr;
    QDoubleSpinBox *m_colormapMinSpin = nullptr;
    QDoubleSpinBox *m_colormapMaxSpin = nullptr;
    QComboBox *m_colormapScaleCombo   = nullptr;
    QPushButton *m_colormapResetBtn   = nullptr;
    QPushButton *m_bgColorButton      = nullptr;
    QPushButton *m_unseenColorButton  = nullptr;
    QCheckBox *m_colorBarCheck        = nullptr;
    QColor m_bgColor                  = QColor(0, 0, 0);
    QColor m_unseenColor              = QColor(0, 0, 0);

    // Mask overlay controls
    QPushButton *m_maskOverlayButton = nullptr;
    QWidget *m_maskListWidget        = nullptr;
    QVBoxLayout *m_maskListLayout    = nullptr;
    QPushButton *m_clearMaskButton   = nullptr;
    QComboBox *m_maskModeCombo       = nullptr;
    QSlider *m_maskOpacitySlider     = nullptr;
    QFormLayout *m_overlaysLayout    = nullptr;
    QLabel *m_fskyLabel              = nullptr;

    // Map info controls
    QLabel *m_nsideLabel = nullptr;
    QLabel *m_npixLabel  = nullptr;
    QLabel *m_avgLabel   = nullptr;
    QLabel *m_rmsLabel   = nullptr;
    QLabel *m_stdLabel   = nullptr;

    // FITS Header info
    // TODO: Add a widget to display FITS header information
};
