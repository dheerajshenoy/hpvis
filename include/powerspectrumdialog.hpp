#pragma once

#include "healpix_map.h"

#include <QDialog>
#include <QString>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QQuickView;
class QLineSeries;

struct Spectra
{
    std::vector<double> TT, EE, BB, TE, TB, EB;
};

// What the dialog needs to know about the current map
struct PowerSpectrumInput
{
    QString path;   // FITS file the map came from (looked at for Q / U columns)
    int hdu = 0;    // 1-based HDU of the map; 0 = unknown
    std::shared_ptr<Healpix_Map<float>> map; // fallback / intensity-only map
    bool maskActive = false; // a mask is applied in the view
};

class PowerSpectrumDialog : public QDialog
{
    Q_OBJECT
public:
    PowerSpectrumDialog(const PowerSpectrumInput &input,
                        QWidget *parent = nullptr);

private:
    void initUI();
    void computePowerSpectrum(const PowerSpectrumInput &input);
    void showSpectra(const Spectra &spectra);
    void updatePlot();

private:
    QQuickView *m_chartView = nullptr;
    QLabel *m_statusLabel   = nullptr;
    QLabel *m_warningLabel  = nullptr;
    QComboBox *m_spectrumCombo = nullptr;
    Spectra m_spectra;
};
