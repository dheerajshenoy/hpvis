#pragma once

#include "healpix_map.h"
#include "maskbuilder.hpp"
#include "skyframe.hpp"

#include <QDockWidget>
#include <QHash>
#include <QPointer>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class View;

// Builds a mask for the current tab from a list of rules (threshold, galactic
// cut, disc), with an optional smooth edge and the sky fraction. The rules
// belong to the tab: each one keeps its own list.
class MaskBuilderDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit MaskBuilderDock(QWidget *parent = nullptr);

    // Shows the rules of `view` (clears the dock when null)
    void setView(View *view);
    // Drops the rules of a view that is going away
    void forget(const View *view);

signals:
    // The mask as a new map (a new tab)
    void mapReady(std::shared_ptr<Healpix_Map<float>> map, const QString &title,
                  SkyFrame frame);

private:
    struct State
    {
        std::vector<maskbuilder::Rule> rules;
        double apodFwhm = 0.0;
        bool applied    = false; // the tab holds this builder's mask
    };

    State *state();
    void addRule(maskbuilder::RuleType type);
    void removeRule();
    void moveRule(int delta);
    void fillList(int select = -1);
    void fillEditor();
    void editorChanged();
    void updateEditorVisibility();
    void scheduleCompute();
    void compute();
    void applyToMap();
    void removeFromMap();
    void showResult(const maskbuilder::Result &r);

    QPointer<View> m_view;
    QHash<const View *, State> m_states;
    std::shared_ptr<Healpix_Map<float>> m_last; // the latest mask
    quint64 m_generation = 0;
    bool m_filling       = false;

    QLabel *m_source           = nullptr;
    QListWidget *m_list        = nullptr;
    QPushButton *m_remove      = nullptr;
    QPushButton *m_up          = nullptr;
    QPushButton *m_down        = nullptr;
    QFormLayout *m_form        = nullptr;
    QComboBox *m_combine       = nullptr;
    QDoubleSpinBox *m_lo       = nullptr;
    QDoubleSpinBox *m_hi       = nullptr;
    QDoubleSpinBox *m_latitude = nullptr;
    QDoubleSpinBox *m_lon      = nullptr;
    QDoubleSpinBox *m_lat      = nullptr;
    QDoubleSpinBox *m_radius   = nullptr;
    QComboBox *m_discMode      = nullptr;
    QDoubleSpinBox *m_apod     = nullptr;
    QLabel *m_fsky             = nullptr;
    QLabel *m_message          = nullptr;
    QCheckBox *m_live          = nullptr;
    QPushButton *m_apply       = nullptr;
    QPushButton *m_unapply     = nullptr;
    QPushButton *m_openMap     = nullptr;
    QTimer *m_timer            = nullptr;
};
