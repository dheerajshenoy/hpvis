#pragma once

#include "healpix_map.h"

#include <QDialog>

class QQuickView;
class QLineSeries;

struct Spectra
{
    std::vector<double> TT, EE, BB, TE, TB, EB;
};

class PowerSpectrumDialog : public QDialog
{
    Q_OBJECT
public:
    PowerSpectrumDialog(const Healpix_Map<float> *map,
                        QWidget *parent = nullptr);

private:
    void initUI();
    void computePowerSpectrum(const Healpix_Map<float> *map);

private:
    QQuickView *m_chartView = nullptr;
};
