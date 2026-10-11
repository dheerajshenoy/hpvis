#include "settingsdialog.hpp"

#include "colormapregistry.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringListModel>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    initUI();
    initConnections();
    loadSettings();
}

void
SettingsDialog::initUI()
{
    QVBoxLayout *layout = new QVBoxLayout();

    QHBoxLayout *hlayout = new QHBoxLayout();

    m_listModel = new QStringListModel(this);

    m_listModel->setStringList({
        "General",
        "Appearance",
        "Overlays",
        "About",
    });
    m_pageKeywords = {
        "general files recent history list folder directory remember last "
        "interface toolbar status bar show hide",
        "appearance new maps defaults projection mollweide orthographic "
        "colormap animate "
        "color scale linear log reverse background unseen globe shadow limb "
        "darkening",
        "overlays color bar minimap graticule grid spacing color masks mask "
        "mode "
        "hide dim strength opacity",
        "about version author license",
    };

    m_listView = new QListView();

    m_listView->setModel(m_listModel);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_listView->setUniformItemSizes(true);
    m_listView->setFixedWidth(130);

    m_stackedWidget = new QStackedWidget();

    m_stackedWidget->addWidget(createGeneralSettingsPage());
    m_stackedWidget->addWidget(createAppearanceSettingsPage());
    m_stackedWidget->addWidget(createOverlaysSettingsPage());
    m_stackedWidget->addWidget(createAboutPage());

    hlayout->addWidget(m_listView);
    hlayout->addWidget(m_stackedWidget);

    m_searchLineEdit = new QLineEdit();
    m_searchLineEdit->setPlaceholderText("Search settings...");

    layout->addWidget(m_searchLineEdit);
    layout->addLayout(hlayout);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_saveButton              = new QPushButton("Save");
    m_closeButton             = new QPushButton("Close");

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addWidget(m_closeButton);

    layout->addLayout(buttonLayout);

    m_saveButton->setDefault(true);
    setLayout(layout);
    setWindowTitle("Settings");
    resize(620, 460);

    m_listView->setCurrentIndex(m_listModel->index(0));
}

void
SettingsDialog::initConnections()
{
    connect(m_searchLineEdit, &QLineEdit::textChanged, this,
            &SettingsDialog::onSearchTextChanged);

    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SettingsDialog::onListViewSelectionChanged);

    connect(m_saveButton, &QPushButton::clicked, this,
            &SettingsDialog::onSaveClicked);

    connect(m_closeButton, &QPushButton::clicked, this,
            &SettingsDialog::onCloseClicked);

    auto changed = [this]()
    {
        updateSaveEnabled();
    };
    connect(m_recentMaxSpin, &QSpinBox::valueChanged, this, changed);
    connect(m_colormapCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_scaleCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_projectionCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_maskModeCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_maskOpacitySlider, &QSlider::valueChanged, this, changed);
    connect(m_graticuleStepCombo, &QComboBox::currentIndexChanged, this,
            changed);
    for (QCheckBox *box :
         {m_rememberDirCheck, m_showToolbarCheck, m_showStatusBarCheck,
          m_reverseCheck, m_colorBarCheck, m_minimapCheck, m_graticuleCheck,
          m_globeShadowCheck, m_limbDarkeningCheck, m_animateCheck})
        connect(box, &QCheckBox::toggled, this, changed);
}

void
SettingsDialog::onSearchTextChanged(const QString &text)
{
    // Hide the pages that have nothing matching the text
    int firstVisible = -1;
    for (int row = 0; row < m_listModel->rowCount(); ++row)
    {
        const bool match
            = text.isEmpty()
              || m_listModel->index(row).data().toString().contains(
                  text, Qt::CaseInsensitive)
              || m_pageKeywords.value(row).contains(text, Qt::CaseInsensitive);
        m_listView->setRowHidden(row, !match);
        if (match && firstVisible < 0)
            firstVisible = row;
    }

    // Move off a page that just disappeared
    const int current = m_listView->currentIndex().row();
    if (firstVisible >= 0 && m_listView->isRowHidden(current))
        m_listView->setCurrentIndex(m_listModel->index(firstVisible));
}

void
SettingsDialog::onListViewSelectionChanged(const QModelIndex &current,
                                           const QModelIndex &previous)
{
    (void)previous; // Placeholder to avoid unused parameter warning

    if (!current.isValid())
        return;

    // Switch the stacked widget to show the selected settings page
    m_stackedWidget->setCurrentIndex(current.row());
}

void
SettingsDialog::onCloseClicked()
{
    reject();
}

void
SettingsDialog::reject()
{
    if (isModified())
    {
        const auto answer = QMessageBox::question(
            this, "Unsaved changes",
            "You have unsaved changes. Save them before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save)
            onSaveClicked();
    }
    QDialog::reject();
}

void
SettingsDialog::onSaveClicked()
{
    saveSettings();
    emit settingsSaved();
}

AppSettings
SettingsDialog::currentSettings() const
{
    AppSettings a;
    a.recentFilesMax     = m_recentMaxSpin->value();
    a.rememberLastDir    = m_rememberDirCheck->isChecked();
    a.showToolbar        = m_showToolbarCheck->isChecked();
    a.showStatusBar      = m_showStatusBarCheck->isChecked();
    a.projection         = Projection(m_projectionCombo->currentIndex());
    a.graticuleColor     = m_graticuleColor;
    a.maskMode           = m_maskModeCombo->currentIndex();
    a.maskOpacityPercent = m_maskOpacitySlider->value();
    a.colormap           = Colormap(m_colormapCombo->currentIndex());
    a.reverseColormap    = m_reverseCheck->isChecked();
    a.scale              = ColormapScale(m_scaleCombo->currentIndex());
    a.background         = m_background;
    a.unseen             = m_unseen;
    a.colorBar           = m_colorBarCheck->isChecked();
    a.minimap            = m_minimapCheck->isChecked();
    a.graticule          = m_graticuleCheck->isChecked();
    a.graticuleStep      = m_graticuleStepCombo->currentData().toDouble();
    a.globeShadow        = m_globeShadowCheck->isChecked();
    a.limbDarkening      = m_limbDarkeningCheck->isChecked();
    a.animateOrthoGlobe  = m_animateCheck->isChecked();
    return a;
}

bool
SettingsDialog::isModified() const
{
    return !(currentSettings() == m_saved);
}

void
SettingsDialog::updateSaveEnabled()
{
    m_saveButton->setEnabled(isModified());
}

void
SettingsDialog::loadSettings()
{
    const AppSettings a = AppSettings::load();

    m_recentMaxSpin->setValue(a.recentFilesMax);

    m_rememberDirCheck->setChecked(a.rememberLastDir);
    m_showToolbarCheck->setChecked(a.showToolbar);
    m_showStatusBarCheck->setChecked(a.showStatusBar);
    m_projectionCombo->setCurrentIndex(int(a.projection));
    m_graticuleColor = a.graticuleColor;
    setColorButton(m_graticuleColorButton, m_graticuleColor);
    m_maskModeCombo->setCurrentIndex(a.maskMode);
    m_maskOpacitySlider->setValue(a.maskOpacityPercent);

    m_colormapCombo->setCurrentIndex(int(a.colormap));
    m_reverseCheck->setChecked(a.reverseColormap);
    m_scaleCombo->setCurrentIndex(int(a.scale));
    m_background = a.background;
    m_unseen     = a.unseen;
    setColorButton(m_backgroundButton, m_background);
    setColorButton(m_unseenButton, m_unseen);
    m_colorBarCheck->setChecked(a.colorBar);
    m_minimapCheck->setChecked(a.minimap);
    m_graticuleCheck->setChecked(a.graticule);
    const int step = m_graticuleStepCombo->findData(a.graticuleStep);
    m_graticuleStepCombo->setCurrentIndex(step >= 0 ? step : 3);
    m_globeShadowCheck->setChecked(a.globeShadow);
    m_limbDarkeningCheck->setChecked(a.limbDarkening);
    m_animateCheck->setChecked(a.animateOrthoGlobe);

    m_saved = currentSettings(); // normalised by the widgets (e.g. step)
    updateSaveEnabled();
}

void
SettingsDialog::saveSettings()
{
    m_saved = currentSettings();
    m_saved.save();
    updateSaveEnabled();
}

void
SettingsDialog::setColorButton(QPushButton *button, const QColor &color)
{
    button->setStyleSheet(
        QString("background-color: %1; border: 1px solid gray;")
            .arg(color.name()));
}

void
SettingsDialog::pickColor(QPushButton *button, QColor &color,
                          const QString &title)
{
    const QColor chosen = QColorDialog::getColor(color, this, title);
    if (!chosen.isValid())
        return;
    color = chosen;
    setColorButton(button, color);
    updateSaveEnabled();
}

QWidget *
SettingsDialog::createGeneralSettingsPage()
{
    QWidget *page      = new QWidget();
    QVBoxLayout *outer = new QVBoxLayout(page);

    // --- Files ---
    auto *box  = new QGroupBox("Files");
    auto *form = new QFormLayout(box);

    m_recentMaxSpin = new QSpinBox();
    m_recentMaxSpin->setRange(0, 50);
    m_recentMaxSpin->setToolTip("How many files File > Open Recent remembers "
                                "(0 turns it off)");
    form->addRow("Recent files:", m_recentMaxSpin);

    m_rememberDirCheck = new QCheckBox("Open and export dialogs start in the "
                                       "last used folder");
    form->addRow(m_rememberDirCheck);
    outer->addWidget(box);

    // --- Interface ---
    auto *uiBox  = new QGroupBox("Interface");
    auto *uiForm = new QFormLayout(uiBox);

    m_showToolbarCheck = new QCheckBox("Show toolbar");
    uiForm->addRow(m_showToolbarCheck);

    m_showStatusBarCheck = new QCheckBox("Show status bar");
    uiForm->addRow(m_showStatusBarCheck);
    outer->addWidget(uiBox);

    outer->addStretch();
    return page;
}

QWidget *
SettingsDialog::createAppearanceSettingsPage()
{
    QWidget *page      = new QWidget();
    QVBoxLayout *outer = new QVBoxLayout(page);

    outer->addWidget(new QLabel(
        "Changes here apply to all open maps and to maps you open later. Only "
        "the options you change are applied, so other per-map tweaks are "
        "kept."));

    // --- Projection ---
    auto *projBox     = new QGroupBox("Projection");
    auto *projForm    = new QFormLayout(projBox);
    m_projectionCombo = new QComboBox();
    m_projectionCombo->addItems({"Mollweide", "Plate Carr\u00e9e",
                                 "Orthographic", "Azimuthal Equidistant",
                                 "Gnomonic"});
    projForm->addRow("Default projection:", m_projectionCombo);
    outer->addWidget(projBox);

    // --- Color ---
    auto *colorBox = new QGroupBox("Color");
    auto *form     = new QFormLayout(colorBox);

    m_colormapCombo = new QComboBox();
    m_colormapCombo->addItems(ColormapRegistry::instance().names());
    form->addRow("Colormap:", m_colormapCombo);

    m_reverseCheck = new QCheckBox("Reverse");
    form->addRow(m_reverseCheck);

    m_scaleCombo = new QComboBox();
    m_scaleCombo->addItems({"Linear", "Logarithmic"});
    form->addRow("Scale:", m_scaleCombo);

    m_backgroundButton = new QPushButton();
    m_backgroundButton->setFixedHeight(22);
    connect(m_backgroundButton, &QPushButton::clicked, this, [this]()
    { pickColor(m_backgroundButton, m_background, "Background Color"); });
    form->addRow("Background:", m_backgroundButton);

    m_unseenButton = new QPushButton();
    m_unseenButton->setFixedHeight(22);
    connect(m_unseenButton, &QPushButton::clicked, this,
            [this]() { pickColor(m_unseenButton, m_unseen, "Unseen Color"); });
    form->addRow("Unseen:", m_unseenButton);

    outer->addWidget(colorBox);

    // --- Orthographic ---
    auto *orthoBox  = new QGroupBox("Orthographic");
    auto *orthoForm = new QFormLayout(orthoBox);

    m_globeShadowCheck = new QCheckBox("Shadow around globe");
    orthoForm->addRow(m_globeShadowCheck);

    m_limbDarkeningCheck = new QCheckBox("Limb darkening");
    orthoForm->addRow(m_limbDarkeningCheck);

    auto *animationBox  = new QGroupBox("Animation");
    auto *animationForm = new QFormLayout(animationBox);
    m_animateCheck      = new QCheckBox("Enable Ortho Sphere Animation");
    animationForm->addRow(m_animateCheck);

    outer->addWidget(orthoBox);
    outer->addWidget(animationBox);

    outer->addStretch();
    return page;
}

QWidget *
SettingsDialog::createOverlaysSettingsPage()
{
    QWidget *page      = new QWidget();
    QVBoxLayout *outer = new QVBoxLayout(page);

    // --- Display ---
    auto *displayBox  = new QGroupBox("Display");
    auto *displayForm = new QFormLayout(displayBox);

    m_colorBarCheck = new QCheckBox("Show color bar");
    displayForm->addRow(m_colorBarCheck);

    m_minimapCheck = new QCheckBox("Show minimap");
    displayForm->addRow(m_minimapCheck);

    outer->addWidget(displayBox);

    // --- Graticule ---
    auto *gratBox  = new QGroupBox("Graticule");
    auto *gratForm = new QFormLayout(gratBox);

    m_graticuleCheck = new QCheckBox("Show graticule");
    gratForm->addRow(m_graticuleCheck);

    m_graticuleStepCombo = new QComboBox();
    for (double deg : {10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 90.0})
        m_graticuleStepCombo->addItem(QString("%1\u00b0").arg(deg), deg);
    gratForm->addRow("Grid spacing:", m_graticuleStepCombo);

    m_graticuleColorButton = new QPushButton();
    m_graticuleColorButton->setFixedHeight(22);
    connect(m_graticuleColorButton, &QPushButton::clicked, this, [this]() {
        pickColor(m_graticuleColorButton, m_graticuleColor, "Graticule Color");
    });
    gratForm->addRow("Color:", m_graticuleColorButton);

    // The spacing only matters while the graticule is on
    m_graticuleStepCombo->setEnabled(false);
    connect(m_graticuleCheck, &QCheckBox::toggled, m_graticuleStepCombo,
            &QWidget::setEnabled);

    outer->addWidget(gratBox);

    // --- Masks ---
    auto *maskBox  = new QGroupBox("Masks");
    auto *maskForm = new QFormLayout(maskBox);

    m_maskModeCombo = new QComboBox();
    m_maskModeCombo->addItems({"Hide", "Dim"});
    maskForm->addRow("Masked pixels:", m_maskModeCombo);

    m_maskOpacitySlider = new QSlider(Qt::Horizontal);
    m_maskOpacitySlider->setRange(0, 100);
    maskForm->addRow("Dim strength:", m_maskOpacitySlider);

    // The strength only applies to "Dim"
    m_maskOpacitySlider->setEnabled(false);
    connect(m_maskModeCombo, &QComboBox::currentIndexChanged,
            m_maskOpacitySlider,
            [this](int mode) { m_maskOpacitySlider->setEnabled(mode == 1); });

    outer->addWidget(maskBox);
    outer->addStretch();
    return page;
}

QWidget *
SettingsDialog::createAboutPage()
{
    QWidget *page = new QWidget();

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addStretch();

    QLabel *aboutLabel
        = new QLabel("<h2>hpvis</h2>"
                     "<p>Version: " PROJECT_VERSION "</p>"
                     "<p>A healpix viewer.</p>"
                     "<p>Developed by Dheeraj Vittal Shenoy.</p>"
                     "<a href=https://github.com/dheerajshenoy/hpvis\">GitHub "
                     "repository</a>"
                     "<p>Licensed under the AGPL-3.0 License.</p>"
                     "information.</p>");
    aboutLabel->setTextFormat(Qt::RichText);
    aboutLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    aboutLabel->setOpenExternalLinks(true);
    layout->addWidget(aboutLabel);
    layout->addStretch();

    return page;
}
