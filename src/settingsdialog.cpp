#include "settingsdialog.hpp"
#include "aboutcontent.hpp"

#include "colormapregistry.hpp"
#include "theoryspectrum.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QScrollArea>
#include <QTableWidget>
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

SettingsDialog::SettingsDialog(const QList<ShortcutCommand> &commands,
                               QWidget *parent)
    : QDialog(parent), m_commands(commands)
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
        "Theory",
        "Shortcuts",
        "About",
    });
    m_pageKeywords = {
        "general files recent history list folder directory remember last "
        "interface toolbar status bar show hide hdr rendering float "
        "framebuffer banding",
        "appearance power spectrum plot background foreground line colors export svg pdf new "
        "maps defaults projection perspective inside outside sky globe mirror "
        "orientation free galactic ecliptic equatorial frame coordinates "
        "mollweide orthographic "
        "colormap animate "
        "color scale linear log reverse background unseen globe shadow limb "
        "darkening",
        "overlays color bar minimap graticule grid spacing color masks mask "
        "mode "
        "hide dim strength opacity",
        "theory theoretical cmb power spectrum planck 2018 best fit cosmology "
        "camb class custom file beam overplot units microkelvin kelvin",
        "shortcuts keyboard keys keybindings bindings hotkeys commands menu "
        "customize remap conflict reset",
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
    m_stackedWidget->addWidget(createTheorySettingsPage());
    m_stackedWidget->addWidget(createShortcutsPage());
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
    connect(m_streamCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_theoryPresetCombo, &QComboBox::currentIndexChanged, this,
            [this, changed]()
    {
        const bool custom = m_theoryPresetCombo->currentIndex() == 1;
        m_theoryFile->setEnabled(custom);
        m_theoryBrowse->setEnabled(custom);
        m_cosmoBox->setEnabled(!custom);
        changed();
    });
    auto cosmoChanged = [this, changed]()
    {
        updateCosmoStatus();
        changed();
    };
    for (QDoubleSpinBox *spin : m_cosmoSpin)
        connect(spin, &QDoubleSpinBox::valueChanged, this, cosmoChanged);
    connect(m_ntConsistency, &QCheckBox::toggled, this, cosmoChanged);
    connect(m_yheFromBbn, &QCheckBox::toggled, this, cosmoChanged);
    connect(m_cosmoReset, &QPushButton::clicked, this,
            [this]() { setCosmoControls(CosmoParams()); });
    connect(m_theoryUnitCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_theoryFile, &QLineEdit::textChanged, this, changed);
    connect(m_theoryBrowse, &QPushButton::clicked, this, [this]()
    {
        const QString f = QFileDialog::getOpenFileName(
            this, "Theory spectrum", QString(),
            "Text files (*.txt *.dat *.csv);;All Files (*)");
        if (!f.isEmpty())
            m_theoryFile->setText(f);
    });
    connect(m_colormapCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_scaleCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_projectionCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_perspectiveCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_orientationCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_maskModeCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_maskOpacitySlider, &QSlider::valueChanged, this, changed);
    connect(m_graticuleStepCombo, &QComboBox::currentIndexChanged, this,
            changed);
    for (QCheckBox *box :
         {m_rememberDirCheck, m_restoreSessionCheck, m_showToolbarCheck, m_showStatusBarCheck,
          m_reverseCheck, m_colorBarCheck, m_minimapCheck, m_graticuleCheck,
          m_globeShadowCheck, m_limbDarkeningCheck, m_animateCheck,
          m_animateProjectionCheck, m_hdrCheck, m_exportColorsCheck})
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
    // A key typed a moment ago may not have been committed yet
    if (m_lastEditedShortcut >= 0)
        commitShortcut(m_lastEditedShortcut);
    saveSettings();
    emit settingsSaved();
    computeTheoryInBackground();
}

CosmoParams
SettingsDialog::cosmoFromControls() const
{
    CosmoParams p;
    for (size_t i = 0; i < std::size(cosmoFields); ++i)
        p.*cosmoFields[i].member = m_cosmoSpin[int(i)]->value();
    p.ntConsistency = m_ntConsistency->isChecked();
    p.yheFromBbn    = m_yheFromBbn->isChecked();
    return p;
}

void
SettingsDialog::setCosmoControls(const CosmoParams &p)
{
    for (size_t i = 0; i < std::size(cosmoFields); ++i)
        m_cosmoSpin[int(i)]->setValue(p.*cosmoFields[i].member);
    m_ntConsistency->setChecked(p.ntConsistency);
    m_yheFromBbn->setChecked(p.yheFromBbn);
    updateCosmoStatus();
}

void
SettingsDialog::updateCosmoStatus()
{
    if (m_theoryComputing)
        return;
    const CosmoParams p = cosmoFromControls();
    m_cosmoReset->setEnabled(!p.isDefault());
    QString text;
    if (p.isDefault())
        text = "Planck 2018 best fit: the bundled spectrum is used.";
    else if (theoryTableCached(p))
        text = "Custom parameters: the spectrum computed with CLASS is ready.";
    else
        text = "Custom parameters: the spectrum is computed with CLASS when "
               "you save (it takes up to a minute, then it is kept).";
    m_cosmoStatus->setText(text);
}

// Computes the spectrum of custom parameters right after saving, so that the
// power spectrum dock and random maps find it ready
void
SettingsDialog::computeTheoryInBackground()
{
    const AppSettings a = AppSettings::load();
    if (m_theoryComputing || a.theoryPreset != 0
        || theoryTableCached(a.theoryCosmo))
    {
        updateCosmoStatus();
        return;
    }
    m_theoryComputing = true;
    m_cosmoStatus->setText("Computing the spectrum with CLASS...");
    const CosmoParams params = a.theoryCosmo;
    auto *watcher            = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher]()
    {
        const QString error = watcher->result();
        watcher->deleteLater();
        m_theoryComputing = false;
        updateCosmoStatus();
        if (!error.isEmpty())
        {
            m_cosmoStatus->setText("CLASS failed: " + error);
            m_cosmoStatus->setStyleSheet("color: #d33;");
        }
        else
            m_cosmoStatus->setStyleSheet(QString());
    });
    watcher->setFuture(QtConcurrent::run([params]() -> QString
    {
        try
        {
            ensureTheoryTable(params);
            return QString();
        }
        catch (const std::exception &e)
        {
            return QString::fromUtf8(e.what());
        }
    }));
}

AppSettings
SettingsDialog::currentSettings() const
{
    AppSettings a;
    a.recentFilesMax     = m_recentMaxSpin->value();
    a.rememberLastDir    = m_rememberDirCheck->isChecked();
    a.restoreSession     = m_restoreSessionCheck->isChecked();
    a.showToolbar        = m_showToolbarCheck->isChecked();
    a.showStatusBar      = m_showStatusBarCheck->isChecked();
    a.streamingNside     = m_streamCombo->currentData().toInt();
    a.projection         = Projection(m_projectionCombo->currentIndex());
    a.viewFromOutside    = m_perspectiveCombo->currentIndex() == 1;
    a.orientation        = Orientation(m_orientationCombo->currentIndex());
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
    a.animateProjection  = m_animateProjectionCheck->isChecked();
    a.hdrRendering       = m_hdrCheck->isChecked();
    a.theoryPreset       = m_theoryPresetCombo->currentIndex();
    a.theoryFile         = m_theoryFile->text().trimmed();
    a.theoryUnit         = m_theoryUnitCombo->currentIndex();
    a.theoryCosmo        = cosmoFromControls();
    a.plotBackground     = m_plotBackground;
    a.plotForeground     = m_plotForeground;
    a.plotLine           = m_plotLine;
    a.exportPlotColors   = m_exportColorsCheck->isChecked();
    // Only the keys that differ from the built-in ones are stored
    for (int i = 0; i < m_commands.size(); ++i)
    {
        const QKeySequence key = m_keyEdits[i]->keySequence();
        if (key != m_commands[i].defaultKey)
            a.shortcuts.insert(m_commands[i].id,
                               key.toString(QKeySequence::PortableText));
    }
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
    m_restoreSessionCheck->setChecked(a.restoreSession);
    m_showToolbarCheck->setChecked(a.showToolbar);
    m_showStatusBarCheck->setChecked(a.showStatusBar);
    const int stream = m_streamCombo->findData(a.streamingNside);
    m_streamCombo->setCurrentIndex(stream >= 0 ? stream : 3);
    m_projectionCombo->setCurrentIndex(int(a.projection));
    m_perspectiveCombo->setCurrentIndex(a.viewFromOutside ? 1 : 0);
    m_orientationCombo->setCurrentIndex(int(a.orientation));
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
    m_animateProjectionCheck->setChecked(a.animateProjection);
    m_hdrCheck->setChecked(a.hdrRendering);
    m_theoryPresetCombo->setCurrentIndex(a.theoryPreset);
    m_theoryFile->setText(a.theoryFile);
    m_theoryUnitCombo->setCurrentIndex(a.theoryUnit);
    m_theoryFile->setEnabled(a.theoryPreset == 1);
    m_theoryBrowse->setEnabled(a.theoryPreset == 1);
    m_cosmoBox->setEnabled(a.theoryPreset != 1);
    setCosmoControls(a.theoryCosmo);
    m_plotBackground = a.plotBackground;
    m_plotForeground = a.plotForeground;
    m_plotLine       = a.plotLine;
    setColorButton(m_plotBgButton, m_plotBackground);
    setColorButton(m_plotFgButton, m_plotForeground);
    setColorButton(m_plotLineButton, m_plotLine);
    m_exportColorsCheck->setChecked(a.exportPlotColors);

    for (int i = 0; i < m_commands.size(); ++i)
    {
        const auto it = a.shortcuts.constFind(m_commands[i].id);
        const QSignalBlocker block(m_keyEdits[i]);
        m_keyEdits[i]->setKeySequence(
            it == a.shortcuts.cend()
                ? m_commands[i].defaultKey
                : QKeySequence(it.value(), QKeySequence::PortableText));
    }

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

    m_restoreSessionCheck = new QCheckBox("Reopen the files that were open "
                                          "when the program was closed");
    m_restoreSessionCheck->setToolTip(
        "At start-up, when no file is given on the command line. File > "
        "Reopen Last Session does it on demand.");
    form->addRow(m_restoreSessionCheck);
    outer->addWidget(box);

    // --- Large maps ---
    auto *bigBox  = new QGroupBox("Large maps");
    auto *bigForm = new QFormLayout(bigBox);

    m_streamCombo = new QComboBox();
    for (int n : {512, 1024, 2048, 4096, 8192})
        m_streamCombo->addItem(n == 8192 ? "8192 (never stream)"
                                         : QString::number(n),
                               n);
    m_streamCombo->setToolTip(
        "Maps with a larger nside are not kept on the GPU in full: a coarse "
        "copy (at most nside 2048) stays resident and only the full-resolution "
        "tiles on screen are loaded as you zoom and pan. Lowers video memory "
        "use to a few hundred MB whatever the nside. Masks are shown at the "
        "coarse resolution while streaming. Applies to open maps too.");
    bigForm->addRow("Stream maps above nside:", m_streamCombo);
    outer->addWidget(bigBox);

    auto *renderBox  = new QGroupBox("Rendering");
    auto *renderForm = new QFormLayout(renderBox);
    m_hdrCheck = new QCheckBox("HDR rendering (16-bit float framebuffer)");
    m_hdrCheck->setToolTip(
        "Avoids banding and clipping on smooth gradients. Turn off if the "
        "display glitches on old GPUs. Applies to newly opened tabs after a "
        "restart.");
    renderForm->addRow(m_hdrCheck);
    outer->addWidget(renderBox);

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
    m_projectionCombo->addItems({"Mollweide", "Equirectangular",
                                 "Orthographic", "Azimuthal Equidistant",
                                 "Gnomonic", "Mercator", "Sinusoidal",
                                 "Lambert Cylindrical", "Gall-Peters",
                                 "Stereographic", "Hammer", "Aitoff", "Werner",
                                 "Lambert Azimuthal"});
    projForm->addRow("Default projection:", m_projectionCombo);

    m_perspectiveCombo = new QComboBox();
    m_perspectiveCombo->addItems({"Inside (sky)", "Outside (globe)"});
    m_perspectiveCombo->setToolTip(
        "Inside: the sky seen from the center, longitude increasing to the "
        "left.\nOutside: the mirror image, like a globe, longitude "
        "increasing to the right.");
    projForm->addRow("Perspective:", m_perspectiveCombo);

    m_orientationCombo = new QComboBox();
    m_orientationCombo->addItems(
        {"Default", "Galactic", "Ecliptic", "Equatorial"});
    m_orientationCombo->setToolTip(
        "Default: the map's own frame, dragging also rolls the view.\n"
        "Galactic, Ecliptic, Equatorial: show the sky in that frame and keep "
        "its north up while dragging.");
    projForm->addRow("Orientation:", m_orientationCombo);
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
    m_scaleCombo->addItems({"Linear", "Logarithmic", "Asinh"});
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
    m_animateCheck      = new QCheckBox(
        "Orthographic globe: spin after dragging, glide on reset");
    animationForm->addRow(m_animateCheck);
    m_animateProjectionCheck
        = new QCheckBox("Fade between projections when switching");
    animationForm->addRow(m_animateProjectionCheck);

    // --- Power spectrum plot ---
    auto *plotBox     = new QGroupBox("Power spectrum plot");
    auto *plotForm    = new QFormLayout(plotBox);
    auto addPlotColor = [&](QPushButton *&button, QColor &color,
                            const QString &label, const QString &title)
    {
        button = new QPushButton();
        button->setFixedHeight(22);
        connect(button, &QPushButton::clicked, this,
                [this, &button, &color, title]()
        { pickColor(button, color, title); });
        plotForm->addRow(label, button);
    };
    addPlotColor(m_plotBgButton, m_plotBackground,
                 "Background:", "Plot Background");
    addPlotColor(m_plotFgButton, m_plotForeground,
                 "Foreground:", "Plot Foreground (text and axes)");
    addPlotColor(m_plotLineButton, m_plotLine, "Line:", "Plot Line");
    m_exportColorsCheck = new QCheckBox("Keep the plot colors in SVG / PDF export");
    m_exportColorsCheck->setToolTip(
        "On: exported plots use the colors shown on screen.\n"
        "Off: black on white, with the curve colors darkened for paper.");
    plotForm->addRow(m_exportColorsCheck);

    outer->addWidget(orthoBox);
    outer->addWidget(animationBox);
    outer->addWidget(plotBox);

    outer->addStretch();
    return page;
}

QWidget *
SettingsDialog::createTheorySettingsPage()
{
    QWidget *page      = new QWidget();
    QVBoxLayout *outer = new QVBoxLayout(page);

    auto *box  = new QGroupBox("Theoretical CMB spectrum");
    auto *form = new QFormLayout(box);

    m_theoryPresetCombo = new QComboBox();
    m_theoryPresetCombo->addItems(
        {"Planck 2018 best fit (bundled)", "Custom file"});
    m_theoryPresetCombo->setToolTip(
        "The base-LCDM best fit of Planck 2018 (TT,TE,EE+lowE+lensing) ships "
        "with hpvis: TT, TE, EE and lensing-only BB, l up to 2508.");
    form->addRow("Spectrum:", m_theoryPresetCombo);

    auto *fileRow = new QHBoxLayout();
    m_theoryFile  = new QLineEdit();
    m_theoryFile->setPlaceholderText(
        "text file: l TT TE EE BB, in D_l = l(l+1) C_l / 2pi (uK^2)");
    m_theoryFile->setToolTip(
        "Columns are taken from a # header naming them (L TT TE EE BB ...); "
        "without one the order is L TT TE EE BB. Values are D_l in uK^2, as "
        "CAMB and CLASS write them.");
    m_theoryBrowse = new QPushButton("Browse...");
    fileRow->addWidget(m_theoryFile, 1);
    fileRow->addWidget(m_theoryBrowse);
    form->addRow("File:", fileRow);

    m_theoryUnitCombo = new QComboBox();
    m_theoryUnitCombo->addItems({"uK", "mK", "K"});
    m_theoryUnitCombo->setToolTip(
        "The unit of the maps you analyse, used when the file does not give "
        "one (the FITS TUNIT keyword is used when it does). The theory is in "
        "uK^2 and is scaled to match.");
    form->addRow("Default map unit:", m_theoryUnitCombo);

    auto *note = new QLabel(
        "Overplot it from the power spectrum dock: Compare > Theory.");
    note->setWordWrap(true);
    form->addRow(note);

    outer->addWidget(box);

    // The Planck 2018 spectrum from its parameters: the defaults give the
    // bundled best fit, others are computed with CLASS
    m_cosmoBox      = new QGroupBox("Planck 2018 cosmological parameters");
    auto *cosmoForm = new QFormLayout(m_cosmoBox);
    struct Row
    {
        const char *section; // heading shown before the row (or null)
        const char *label;
        const char *tip;
        double min, max, step;
        int decimals;
    };
    // In the order of cosmoFields
    static const Row rows[] = {
        {"Base LCDM", "H0 [km/s/Mpc]:", "Hubble constant", 20.0, 100.0, 0.1, 3},
        {nullptr, "\u03a9b h\u00b2:", "Physical baryon density", 0.005, 0.1,
         0.0001, 5},
        {nullptr, "\u03a9c h\u00b2:", "Physical cold dark matter density",
         0.01, 0.5, 0.001, 5},
        {nullptr, "\u03c4:", "Optical depth to reionization", 0.01, 0.3, 0.001,
         4},
        {nullptr, "ln(10\u00b9\u2070 A\u209b):",
         "Log of the scalar amplitude", 2.0, 4.0, 0.001, 4},
        {nullptr, "n\u209b:", "Scalar spectral index", 0.8, 1.2, 0.001, 4},
        {"Primordial", "dn\u209b/dln k:", "Running of the spectral index",
         -0.1, 0.1, 0.001, 4},
        {nullptr, "r:",
         "Tensor-to-scalar ratio. Above 0 adds primordial gravitational waves "
         "(B modes at low l); the computation takes longer.",
         0.0, 1.0, 0.001, 4},
        {nullptr, "n\u209c:", "Tensor spectral index", -2.0, 2.0, 0.01, 4},
        {"Neutrinos", "N_eff:", "Effective number of neutrino species", 0.5,
         10.0, 0.01, 3},
        {nullptr, "\u03a3m\u03bd [eV]:",
         "Sum of the neutrino masses, in one massive species. 0 makes them "
         "all massless.",
         0.0, 5.0, 0.01, 3},
        {"Geometry and dark energy", "\u03a9k:", "Curvature density", -0.3,
         0.3, 0.001, 4},
        {nullptr, "w0:",
         "Dark energy equation of state, w = w0 + wa (1 - a). Away from -1 "
         "the cosmological constant is replaced by a dark energy fluid.",
         -3.0, 0.0, 0.01, 3},
        {nullptr, "wa:", "Evolution of the dark energy equation of state", -3.0,
         3.0, 0.01, 3},
        {"Thermal history", "T_cmb [K]:", "CMB temperature today", 2.0, 3.5,
         0.001, 4},
        {nullptr, "Y_He:", "Helium mass fraction", 0.1, 0.5, 0.001, 4}};
    static_assert(std::size(rows) == std::size(cosmoFields));
    for (size_t i = 0; i < std::size(rows); ++i)
    {
        if (rows[i].section)
        {
            auto *heading = new QLabel(QString("<b>%1</b>").arg(rows[i].section));
            cosmoForm->addRow(heading);
        }
        auto *spin = new QDoubleSpinBox();
        spin->setRange(rows[i].min, rows[i].max);
        spin->setDecimals(rows[i].decimals);
        spin->setSingleStep(rows[i].step);
        spin->setToolTip(rows[i].tip);
        m_cosmoSpin.append(spin);
        cosmoForm->addRow(QString::fromUtf8(rows[i].label), spin);
        if (QString(cosmoFields[i].key) == "nt")
        {
            m_ntConsistency = new QCheckBox("n\u209c = -r/8 (single-field)");
            m_ntConsistency->setToolTip(
                "Set the tensor tilt from the single-field consistency "
                "relation");
            cosmoForm->addRow(m_ntConsistency);
            connect(m_ntConsistency, &QCheckBox::toggled, spin,
                    [spin](bool on) { spin->setEnabled(!on); });
        }
        else if (QString(cosmoFields[i].key) == "yhe")
        {
            m_yheFromBbn = new QCheckBox("Y_He from BBN");
            m_yheFromBbn->setToolTip(
                "Compute the helium fraction from the baryon density");
            cosmoForm->addRow(m_yheFromBbn);
            connect(m_yheFromBbn, &QCheckBox::toggled, spin,
                    [spin](bool on) { spin->setEnabled(!on); });
        }
    }
    m_cosmoReset = new QPushButton("Reset to Planck 2018");
    m_cosmoReset->setToolTip("Restore the Planck 2018 best-fit parameters "
                             "(TT,TE,EE+lowE+lensing)");
    cosmoForm->addRow(m_cosmoReset);
    m_cosmoStatus = new QLabel();
    m_cosmoStatus->setWordWrap(true);
    cosmoForm->addRow(m_cosmoStatus);
    outer->addWidget(m_cosmoBox);
    setCosmoControls(CosmoParams());
    outer->addStretch();

    // There are many parameters: the page scrolls
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
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
SettingsDialog::createShortcutsPage()
{
    QWidget *page       = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);

    auto *filter = new QLineEdit();
    filter->setPlaceholderText("Filter commands...");
    layout->addWidget(filter);

    m_shortcutTable = new QTableWidget(int(m_commands.size()), 3);
    m_shortcutTable->setHorizontalHeaderLabels(
        {"Command", "Shortcut", "Default"});
    m_shortcutTable->verticalHeader()->hide();
    m_shortcutTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    layout->addWidget(m_shortcutTable);

    for (int row = 0; row < m_commands.size(); ++row)
    {
        const ShortcutCommand &c = m_commands[row];
        auto *name               = new QTableWidgetItem(
            c.label.isEmpty() ? c.category : c.category + " > " + c.label);
        m_shortcutTable->setItem(row, 0, name);

        auto *edit = new QKeySequenceEdit(c.defaultKey);
        edit->setClearButtonEnabled(true);
        edit->setMinimumWidth(150);
        m_keyEdits.append(edit);
        m_shortcutTable->setCellWidget(row, 1, edit);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this, row]()
        {
            m_lastEditedShortcut = row;
            updateSaveEnabled();
        });
        connect(edit, &QKeySequenceEdit::editingFinished, this,
                [this, row]() { commitShortcut(row); });

        auto *def = new QTableWidgetItem(
            c.defaultKey.isEmpty() ? QString("-")
                                   : c.defaultKey.toString(
                                       QKeySequence::NativeText));
        m_shortcutTable->setItem(row, 2, def);
    }

    m_shortcutNotice = new QLabel();
    m_shortcutNotice->setWordWrap(true);
    layout->addWidget(m_shortcutNotice);

    auto *reset = new QPushButton("Reset All to Defaults");
    layout->addWidget(reset, 0, Qt::AlignRight);

    connect(filter, &QLineEdit::textChanged, this,
            &SettingsDialog::filterShortcuts);
    connect(reset, &QPushButton::clicked, this, [this]()
    {
        for (int i = 0; i < m_commands.size(); ++i)
            m_keyEdits[i]->setKeySequence(m_commands[i].defaultKey);
        m_lastEditedShortcut = -1;
        m_shortcutNotice->clear();
        updateSaveEnabled();
    });
    return page;
}

void
SettingsDialog::commitShortcut(int row)
{
    m_lastEditedShortcut = -1;
    const QKeySequence key = m_keyEdits[row]->keySequence();
    if (key.isEmpty())
    {
        m_shortcutNotice->clear();
        return;
    }

    QStringList lost;
    for (int i = 0; i < m_commands.size(); ++i)
    {
        if (i == row || m_keyEdits[i]->keySequence() != key)
            continue;
        m_keyEdits[i]->setKeySequence(QKeySequence());
        lost << m_shortcutTable->item(i, 0)->text();
    }
    if (lost.isEmpty())
    {
        m_shortcutNotice->clear();
        return;
    }
    m_shortcutNotice->setText(
        QString("%1 now belongs to <b>%2</b>. It was removed from: %3.")
            .arg(key.toString(QKeySequence::NativeText).toHtmlEscaped(),
                 m_shortcutTable->item(row, 0)->text().toHtmlEscaped(),
                 lost.join("; ").toHtmlEscaped()));
    updateSaveEnabled();
}

void
SettingsDialog::filterShortcuts(const QString &text)
{
    for (int row = 0; row < m_shortcutTable->rowCount(); ++row)
        m_shortcutTable->setRowHidden(
            row, !text.isEmpty()
                     && !m_shortcutTable->item(row, 0)->text().contains(
                         text, Qt::CaseInsensitive));
}

QWidget *
SettingsDialog::createAboutPage()
{
    QWidget *page = new QWidget();

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new AboutContent());

    return page;
}
