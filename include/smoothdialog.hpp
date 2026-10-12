#pragma once

#include "healpix_map.h"
#include "skyframe.hpp"

#include <QDialog>
#include <QPointer>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class View;

// Convolves the current map with a Gaussian beam (through the spherical
// harmonics). The result opens as a new tab.
class SmoothDialog : public QDialog
{
    Q_OBJECT
public:
    // `currentView` returns the tab to take the map from (asked each time)
    explicit SmoothDialog(std::function<View *()> currentView,
                          QWidget *parent = nullptr);

    // Reads the current tab again (call when the dialog is shown)
    void refresh();

signals:
    void mapReady(std::shared_ptr<Healpix_Map<float>> map, const QString &title,
                  SkyFrame frame);

private:
    void compute();
    void setBusy(bool busy);
    void showMessage(const QString &text, bool error);
    void updateTitle();
    double fwhmRadians() const;
    void updateOptions();

    std::function<View *()> m_currentView;
    QPointer<View> m_view;
    long m_nside = 0;

    QLabel *m_source             = nullptr;
    QRadioButton *m_gaussRadio   = nullptr;
    QRadioButton *m_customRadio  = nullptr;
    QLineEdit *m_beamFile        = nullptr;
    QPushButton *m_beamBrowse    = nullptr;
    QCheckBox *m_polCheck        = nullptr;
    QDoubleSpinBox *m_fwhm       = nullptr;
    QComboBox *m_unit            = nullptr;
    QSpinBox *m_iterations       = nullptr;
    QSpinBox *m_lmax             = nullptr;
    QCheckBox *m_keepUnseen      = nullptr;
    QLabel *m_hint               = nullptr;
    QLineEdit *m_title           = nullptr;
    QLabel *m_message            = nullptr;
    QProgressBar *m_progress     = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy                  = false;
};
