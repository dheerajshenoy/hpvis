#pragma once

#include "healpix_map.h"
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
class QSpinBox;
class View;

// Laplacian, gradient and E / B maps of the current map, through the
// spherical harmonics. Results open as new tabs.
class HarmonicDialog : public QDialog
{
    Q_OBJECT
public:
    // `currentView` returns the tab to take the map from (asked each time)
    explicit HarmonicDialog(std::function<View *()> currentView,
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
    void updateOperation();

    std::function<View *()> m_currentView;
    QPointer<View> m_view;
    long m_nside = 0;
    bool m_hasPol = false; // the map's HDU has T, Q and U columns

    QLabel *m_source             = nullptr;
    QComboBox *m_operation       = nullptr;
    QSpinBox *m_iterations       = nullptr;
    QSpinBox *m_lmax             = nullptr;
    QCheckBox *m_keepUnseen      = nullptr;
    QLabel *m_message            = nullptr;
    QProgressBar *m_progress     = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy                  = false;
};
