#pragma once

#include "healpix_map.h"
#include "resolution.hpp"
#include "skyframe.hpp"

#include <QDialog>
#include <QPointer>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class View;

// Lowers the resolution of the current map, by averaging pixels (ud_grade) or
// through the spherical harmonics (map2alm, truncate, alm2map). The result
// opens as a new tab.
class DowngradeDialog : public QDialog
{
    Q_OBJECT
public:
    // `currentView` returns the tab to take the map from (asked each time)
    explicit DowngradeDialog(std::function<View *()> currentView,
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
    void updateOptions();
    int nsideOut() const;
    QWidget *windowRow(QLineEdit *&edit, const QString &tip);

    std::function<View *()> m_currentView;
    QPointer<View> m_view;
    long m_nside = 0;

    QLabel *m_source             = nullptr;
    QComboBox *m_nsideCombo      = nullptr;
    QRadioButton *m_pixelRadio   = nullptr;
    QRadioButton *m_harmRadio    = nullptr;
    QCheckBox *m_pessimistic     = nullptr;
    QSpinBox *m_iterations       = nullptr;
    QSpinBox *m_lmax             = nullptr;
    QCheckBox *m_keepUnseen      = nullptr;
    QLineEdit *m_windowIn        = nullptr;
    QLineEdit *m_windowOut       = nullptr;
    QLabel *m_hint               = nullptr;
    QLineEdit *m_title           = nullptr;
    QLabel *m_message            = nullptr;
    QProgressBar *m_progress     = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy                  = false;
};
