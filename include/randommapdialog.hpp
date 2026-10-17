#pragma once

#include "healpix_map.h"
#include "skyframe.hpp"

#include <QDialog>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

// Makes a random map (noise, or a CMB realization of the theory spectrum of
// the settings) and opens it as a new tab.
class RandomMapDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RandomMapDialog(QWidget *parent = nullptr);

signals:
    // `unit` is the label of the unit of the map, empty for noise
    void mapReady(std::shared_ptr<Healpix_Map<float>> map, const QString &title,
                  SkyFrame frame, const QString &unit);

private:
    void compute();
    void updateOptions();
    void showMessage(const QString &text, bool error);

    QComboBox *m_kind            = nullptr;
    QComboBox *m_nside           = nullptr;
    QDoubleSpinBox *m_a          = nullptr;
    QDoubleSpinBox *m_b          = nullptr;
    QLabel *m_aLabel             = nullptr;
    QLabel *m_bLabel             = nullptr;
    QComboBox *m_unit            = nullptr;
    QLabel *m_unitLabel          = nullptr;
    QDoubleSpinBox *m_fwhm       = nullptr;
    QLabel *m_fwhmLabel          = nullptr;
    QCheckBox *m_pixwin          = nullptr;
    QCheckBox *m_randomSeed      = nullptr;
    QSpinBox *m_seed             = nullptr;
    QLabel *m_hint               = nullptr;
    QLabel *m_message            = nullptr;
    QProgressBar *m_progress     = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy                  = false;
};
