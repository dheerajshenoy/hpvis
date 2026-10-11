#pragma once

#include "appsettings.hpp"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QStringListModel;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onSaveClicked();
    void onCancelClicked();
    void onSearchTextChanged(const QString &text);
    void onListViewSelectionChanged(const QModelIndex &current,
                                    const QModelIndex &previous);

private:
    void initUI();
    void initConnections();
    void loadSettings();
    void saveSettings();
    QWidget *createGeneralSettingsPage();
    QWidget *createAppearanceSettingsPage();
    QWidget *createAboutPage();

    void setColorButton(QPushButton *button, const QColor &color);
    void pickColor(QPushButton *button, QColor &color, const QString &title);

private:
    QStringListModel *m_listModel   = nullptr;
    QLineEdit *m_searchLineEdit     = nullptr;
    QPushButton *m_saveButton       = nullptr;
    QPushButton *m_cancelButton     = nullptr;
    QListView *m_listView           = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;

    // Words each page answers to in the search box (same order as the pages)
    QStringList m_pageKeywords;

    // General
    QSpinBox *m_recentMaxSpin = nullptr;

    // Appearance (defaults for new maps)
    QComboBox *m_colormapCombo       = nullptr;
    QCheckBox *m_reverseCheck        = nullptr;
    QComboBox *m_scaleCombo          = nullptr;
    QPushButton *m_backgroundButton  = nullptr;
    QPushButton *m_unseenButton      = nullptr;
    QCheckBox *m_colorBarCheck       = nullptr;
    QCheckBox *m_minimapCheck        = nullptr;
    QCheckBox *m_graticuleCheck      = nullptr;
    QComboBox *m_graticuleStepCombo  = nullptr;
    QColor m_background;
    QColor m_unseen;
};
