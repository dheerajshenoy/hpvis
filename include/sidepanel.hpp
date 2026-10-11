#pragma once

#include "colormap.hpp"
#include "fitsinfo.hpp"
#include "pixelordering.hpp"
#include "projection.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
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

    double graticuleStep() const
    {
        return m_graticuleStepCombo->currentData().toDouble();
    }

    // Show the view's orientation (degrees) without emitting
    // rotationEdited
    void setRotation(double lonDeg, double latDeg, double rollDeg)
    {
        QSignalBlocker b1(m_centerLonSpin);
        QSignalBlocker b2(m_centerLatSpin);
        QSignalBlocker b3(m_rollSpin);
        m_centerLonSpin->setValue(lonDeg);
        m_centerLatSpin->setValue(latDeg);
        m_rollSpin->setValue(rollDeg);
    }

    // Select a projection without emitting projectionChanged
    void setProjection(Projection projection)
    {
        QSignalBlocker blocker(m_projectionCombo);
        const int i = m_projectionCombo->findData(static_cast<int>(projection));
        if (i >= 0)
            m_projectionCombo->setCurrentIndex(i);
    }

    // Mirror a view's state without emitting signals
    void setGraticule(bool enabled, double degrees)
    {
        QSignalBlocker b1(m_graticuleCheckBox);
        QSignalBlocker b2(m_graticuleStepCombo);
        m_graticuleCheckBox->setChecked(enabled);
        updateGraticuleRowVisibility();
        const int i = m_graticuleStepCombo->findData(degrees);
        if (i >= 0)
            m_graticuleStepCombo->setCurrentIndex(i);
    }

    void setMapInfo(qlonglong nside, qlonglong npix, double avg, double rms,
                    double std, double min, double max);

    // Lists the file's table HDUs and their columns, selecting `hdu`/`column`
    // (both 1-based) without emitting fitsSelectionChanged
    void setFitsInfo(const FitsInfo &info, int hdu, int column);

signals:
    void fitsSelectionChanged(int hdu, int column);
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
    void graticuleToggled(bool enabled);
    void graticuleColorChanged(uint32_t color);
    void graticuleStepChanged(double degrees);
    void projectionChanged(Projection index);
    void rotationEdited(double lonDeg, double latDeg, double rollDeg);
    void rotationResetRequested();

private:
    void initUI();
    void initConnections();
    void onBackgroundColorButtonClicked();
    void onUnseenColorButtonClicked();
    void onLoadMaskButtonClicked();
    void updateDimRowVisibility();
    QWidget *pixelOrderingWidget();
    QWidget *colormapWidget();
    QWidget *overlaysWidget();
    QWidget *mapInfoWidget();
    QWidget *projectionWidget();
    void fillColumnCombo(int hdu, int column);
    void updateGraticuleRowVisibility();
    void onGraticuleColorButtonClicked();

private:
    // Map info controls
    QComboBox *m_hduCombo    = nullptr;
    QComboBox *m_columnCombo = nullptr;
    FitsInfo m_fitsInfo;
    QLineEdit *m_nsideEdit = nullptr;
    QLineEdit *m_npixEdit  = nullptr;
    QLineEdit *m_avgEdit   = nullptr;
    QLineEdit *m_rmsEdit   = nullptr;
    QLineEdit *m_minEdit   = nullptr;
    QLineEdit *m_maxEdit   = nullptr;
    QLineEdit *m_stdEdit   = nullptr;

    // Projection controls
    QComboBox *m_projectionCombo = nullptr;
    QDoubleSpinBox *m_centerLonSpin = nullptr;
    QDoubleSpinBox *m_centerLatSpin = nullptr;
    QDoubleSpinBox *m_rollSpin      = nullptr;

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
    QColor m_bgColor                  = QColor(0, 0, 0);
    QColor m_unseenColor              = QColor(0, 0, 0);

    // Mask overlay controls
    QPushButton *m_maskOverlayButton    = nullptr;
    QWidget *m_maskListWidget           = nullptr;
    QVBoxLayout *m_maskListLayout       = nullptr;
    QPushButton *m_clearMaskButton      = nullptr;
    QComboBox *m_maskModeCombo          = nullptr;
    QSlider *m_maskOpacitySlider        = nullptr;
    QFormLayout *m_overlaysLayout       = nullptr;
    QLabel *m_fskyLabel                 = nullptr;
    QCheckBox *m_graticuleCheckBox      = nullptr;
    QComboBox *m_graticuleStepCombo     = nullptr;
    QFormLayout *m_graticuleLayout      = nullptr;
    QPushButton *m_graticuleColorButton = nullptr;
    QColor m_graticuleColor             = QColor(255, 255, 255);

    // FITS Header info
    // TODO: Add a widget to display FITS header information
};
