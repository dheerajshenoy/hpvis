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
class FilterPlot;

// Keeps or removes a range of multipoles of the current map (through the
// spherical harmonics), with a plot of the window f(l). The result opens as a
// new tab.
class MultipoleFilterDialog : public QDialog
{
    Q_OBJECT
public:
    // `currentView` returns the tab to take the map from (asked each time)
    explicit MultipoleFilterDialog(std::function<View *()> currentView,
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
    void updateControls(); // enable the boxes the filter uses, redraw the plot
    int analysisLmax() const;

    std::function<View *()> m_currentView;
    QPointer<View> m_view;
    long m_nside = 0;

    QLabel *m_source             = nullptr;
    QComboBox *m_type            = nullptr;
    QSpinBox *m_lmin             = nullptr;
    QSpinBox *m_lmaxCut          = nullptr;
    QComboBox *m_taper           = nullptr;
    QSpinBox *m_taperWidth       = nullptr;
    FilterPlot *m_plot           = nullptr;
    QCheckBox *m_polCheck        = nullptr;
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
