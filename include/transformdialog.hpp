#pragma once

#include "healpix_map.h"
#include "skyframe.hpp"

#include <QDialog>
#include <QPointer>
#include <functional>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class View;

// Applies a nonlinear transform (log, asinh, histogram equalization, ...) to
// the values of the current map. The result opens as a new tab; the original
// map, and the analysis done on it, are left alone.
class TransformDialog : public QDialog
{
    Q_OBJECT
public:
    // `currentView` returns the tab to take the map from (asked each time)
    explicit TransformDialog(std::function<View *()> currentView,
                             QWidget *parent = nullptr);

    // Reads the current tab again (call when the dialog is shown)
    void refresh();

signals:
    void mapReady(std::shared_ptr<Healpix_Map<float>> map, const QString &title,
                  SkyFrame frame);

private:
    void compute();
    void update(); // enabled state, formula, default title

    std::function<View *()> m_currentView;
    QPointer<View> m_view;

    QLabel *m_source             = nullptr;
    QComboBox *m_kind            = nullptr;
    QLabel *m_formula            = nullptr;
    QDoubleSpinBox *m_mu         = nullptr;
    QDoubleSpinBox *m_sigma      = nullptr;
    QDoubleSpinBox *m_power      = nullptr;
    QPushButton *m_fromMap       = nullptr;
    QLineEdit *m_title           = nullptr;
    QLabel *m_message            = nullptr;
    QProgressBar *m_progress     = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy                  = false;
    double m_mapMean             = 0.0;
    double m_mapStd              = 1.0;
};
