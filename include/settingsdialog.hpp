#pragma once

#include "appsettings.hpp"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QStringListModel;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void reject() override; // asks first when there are unsaved changes

signals:
    void settingsSaved();

private slots:
    void onSaveClicked();
    void onCloseClicked();
    void onSearchTextChanged(const QString &text);
    void onListViewSelectionChanged(const QModelIndex &current,
                                    const QModelIndex &previous);

private:
    void initUI();
    void initConnections();
    void loadSettings();
    void saveSettings();
    AppSettings currentSettings() const;
    bool isModified() const;
    void updateSaveEnabled();
    QWidget *createGeneralSettingsPage();
    QWidget *createAppearanceSettingsPage();
    QWidget *createOverlaysSettingsPage();
    QWidget *createAboutPage();

    void setColorButton(QPushButton *button, const QColor &color);
    void pickColor(QPushButton *button, QColor &color, const QString &title);

private:
    QStringListModel *m_listModel   = nullptr;
    QLineEdit *m_searchLineEdit     = nullptr;
    QPushButton *m_saveButton       = nullptr;
    QPushButton *m_closeButton      = nullptr;
    QListView *m_listView           = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;

    // Words each page answers to in the search box (same order as the pages)
    QStringList m_pageKeywords;

    // General
    QSpinBox *m_recentMaxSpin       = nullptr;
    QComboBox *m_streamCombo        = nullptr;
    QCheckBox *m_rememberDirCheck   = nullptr;
    QCheckBox *m_showToolbarCheck   = nullptr;
    QCheckBox *m_showStatusBarCheck = nullptr;

    // Appearance (defaults for new maps)
    QPushButton *m_plotBgButton   = nullptr;
    QPushButton *m_plotFgButton   = nullptr;
    QPushButton *m_plotLineButton = nullptr;
    QColor m_plotBackground;
    QColor m_plotForeground;
    QColor m_plotLine;
    QComboBox *m_orientationCombo       = nullptr;
    QComboBox *m_perspectiveCombo       = nullptr;
    QComboBox *m_projectionCombo        = nullptr;
    QPushButton *m_graticuleColorButton = nullptr;
    QComboBox *m_maskModeCombo          = nullptr;
    QSlider *m_maskOpacitySlider        = nullptr;
    QColor m_graticuleColor;
    QComboBox *m_colormapCombo      = nullptr;
    QCheckBox *m_reverseCheck       = nullptr;
    QComboBox *m_scaleCombo         = nullptr;
    QPushButton *m_backgroundButton = nullptr;
    QPushButton *m_unseenButton     = nullptr;
    QCheckBox *m_colorBarCheck      = nullptr;
    QCheckBox *m_minimapCheck       = nullptr;
    QCheckBox *m_graticuleCheck     = nullptr;
    QComboBox *m_graticuleStepCombo = nullptr;
    QCheckBox *m_globeShadowCheck   = nullptr;
    QCheckBox *m_limbDarkeningCheck = nullptr;
    QCheckBox *m_animateCheck       = nullptr;
    QCheckBox *m_animateProjectionCheck = nullptr;
    QColor m_background;
    QColor m_unseen;

    AppSettings m_saved; // what is on disk; the Save button tracks the diff
};
