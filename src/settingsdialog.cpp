#include "settingsdialog.hpp"

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
#include <QPushButton>
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
        "About",
    });
    m_pageKeywords = {
        "general recent files history list",
        "appearance new maps defaults colormap color scale linear log reverse "
        "background unseen color bar minimap graticule grid spacing",
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
    m_stackedWidget->addWidget(createAboutPage());

    hlayout->addWidget(m_listView);
    hlayout->addWidget(m_stackedWidget);

    m_searchLineEdit = new QLineEdit();
    m_searchLineEdit->setPlaceholderText("Search settings...");

    layout->addWidget(m_searchLineEdit);
    layout->addLayout(hlayout);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_saveButton              = new QPushButton("Save");
    m_cancelButton            = new QPushButton("Cancel");

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addWidget(m_cancelButton);

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

    connect(m_cancelButton, &QPushButton::clicked, this,
            &SettingsDialog::onCancelClicked);
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
SettingsDialog::onCancelClicked()
{
    reject(); // Close the dialog with a rejected result
}

void
SettingsDialog::onSaveClicked()
{
    saveSettings();
    accept(); // Close the dialog with an accepted result
}

void
SettingsDialog::loadSettings()
{
    const AppSettings a = AppSettings::load();

    m_recentMaxSpin->setValue(a.recentFilesMax);

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
}

void
SettingsDialog::saveSettings()
{
    AppSettings a;
    a.recentFilesMax = m_recentMaxSpin->value();

    a.colormap        = Colormap(m_colormapCombo->currentIndex());
    a.reverseColormap = m_reverseCheck->isChecked();
    a.scale           = ColormapScale(m_scaleCombo->currentIndex());
    a.background      = m_background;
    a.unseen          = m_unseen;
    a.colorBar        = m_colorBarCheck->isChecked();
    a.minimap         = m_minimapCheck->isChecked();
    a.graticule       = m_graticuleCheck->isChecked();
    a.graticuleStep   = m_graticuleStepCombo->currentData().toDouble();
    a.save();
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
}

QWidget *
SettingsDialog::createGeneralSettingsPage()
{
    QWidget *page       = new QWidget();
    QVBoxLayout *outer  = new QVBoxLayout(page);

    auto *box           = new QGroupBox("Files");
    auto *form          = new QFormLayout(box);

    m_recentMaxSpin = new QSpinBox();
    m_recentMaxSpin->setRange(0, 50);
    m_recentMaxSpin->setToolTip("How many files File > Open Recent remembers "
                                "(0 turns it off)");
    form->addRow("Recent files:", m_recentMaxSpin);

    outer->addWidget(box);
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
        "the options you change are applied, so other per-map tweaks are kept."));

    // --- Color ---
    auto *colorBox = new QGroupBox("Color");
    auto *form     = new QFormLayout(colorBox);

    m_colormapCombo = new QComboBox();
    m_colormapCombo->addItems({"Grayscale", "Inferno", "Magma", "Plasma",
                               "Viridis", "Cubehelix", "Jet", "HSV", "Hot",
                               "Cool", "Spring", "Summer", "Autumn", "Winter",
                               "Rainbow"});
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
    connect(m_unseenButton, &QPushButton::clicked, this, [this]()
    { pickColor(m_unseenButton, m_unseen, "Unseen Color"); });
    form->addRow("Unseen:", m_unseenButton);

    outer->addWidget(colorBox);

    // --- Overlays ---
    auto *overlayBox  = new QGroupBox("Overlays");
    auto *overlayForm = new QFormLayout(overlayBox);

    m_colorBarCheck = new QCheckBox("Show color bar");
    overlayForm->addRow(m_colorBarCheck);

    m_minimapCheck = new QCheckBox("Show minimap");
    overlayForm->addRow(m_minimapCheck);

    m_graticuleCheck = new QCheckBox("Show graticule");
    overlayForm->addRow(m_graticuleCheck);

    m_graticuleStepCombo = new QComboBox();
    for (double deg : {10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 90.0})
        m_graticuleStepCombo->addItem(QString("%1\u00b0").arg(deg), deg);
    overlayForm->addRow("Grid spacing:", m_graticuleStepCombo);

    // The spacing only matters while the graticule is on
    m_graticuleStepCombo->setEnabled(false);
    connect(m_graticuleCheck, &QCheckBox::toggled, m_graticuleStepCombo,
            &QWidget::setEnabled);

    outer->addWidget(overlayBox);
    outer->addStretch();
    return page;
}

QWidget *
SettingsDialog::createAboutPage()
{
    QWidget *page = new QWidget();

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addStretch();

    QLabel *aboutLabel = new QLabel(
        "<h2>HpixView</h2>"
        "<p>Version: " PROJECT_VERSION "</p>"
        "<p>A healpix viewer.</p>"
        "<p>Developed by Dheeraj Vittal Shenoy.</p>"
        "<p>Visit <a href='https://dheerajshenoy.github.io/hpixview'>"
        "https://dheerajshenoy.github.io/hpixview</a> for more "
        "information.</p>");
    aboutLabel->setTextFormat(Qt::RichText);
    aboutLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    aboutLabel->setOpenExternalLinks(true);
    layout->addWidget(aboutLabel);
    layout->addStretch();

    return page;
}
