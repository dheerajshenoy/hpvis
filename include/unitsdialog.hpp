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

// Converts the current map between units (K, mK, uK as CMB or Rayleigh-
// Jeans temperatures, Jy/sr, MJy/sr). The result opens as a new tab.
class UnitsDialog : public QDialog
{
    Q_OBJECT
public:
    // `currentView` returns the tab to take the map from (asked each time)
    explicit UnitsDialog(std::function<View *()> currentView,
                         QWidget *parent = nullptr);

    // Reads the current tab again (call when the dialog is shown)
    void refresh();

signals:
    // `unit` is the label of the unit of the new map
    void mapReady(std::shared_ptr<Healpix_Map<float>> map, const QString &title,
                  SkyFrame frame, const QString &unit);

private:
    void compute();
    void update(); // factor, enabled state, default title
    double frequencyGHz() const;

    std::function<View *()> m_currentView;
    QPointer<View> m_view;

    QLabel *m_source             = nullptr;
    QComboBox *m_from            = nullptr;
    QComboBox *m_to              = nullptr;
    QDoubleSpinBox *m_frequency  = nullptr;
    QComboBox *m_frequencyUnit   = nullptr;
    QLabel *m_factor             = nullptr;
    QLineEdit *m_title           = nullptr;
    QLabel *m_message            = nullptr;
    QProgressBar *m_progress     = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy                  = false;
};
