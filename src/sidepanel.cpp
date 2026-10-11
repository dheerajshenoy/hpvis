#include "sidepanel.hpp"

#include "collapsiblesection.hpp"

#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <qpushbutton.h>

SidePanel::SidePanel(QWidget *parent) : QDockWidget(parent)
{
    initUI();
    initConnections();
}

void
SidePanel::initUI()
{
    // setMinimumWidth(150);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *container  = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    layout->addWidget(new CollapsibleSection("Map Info", mapInfoWidget()));
    layout->addWidget(
        new CollapsibleSection("Pixel Ordering", pixelOrderingWidget()));
    layout->addWidget(new CollapsibleSection("Color", colormapWidget()));
    layout->addWidget(new CollapsibleSection("Overlays", overlaysWidget()));
    layout->addWidget(new CollapsibleSection("Projection", projectionWidget()));
    layout->addStretch();

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(container);
    setWidget(scrollArea);
}

void
SidePanel::initConnections()
{
    connect(m_graticuleColorButton, &QPushButton::clicked, this,
            &SidePanel::onGraticuleColorButtonClicked);

    connect(m_colormapCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    { emit colormapChanged(static_cast<Colormap>(index)); });

    connect(m_unseenColorButton, &QPushButton::clicked, this,
            &SidePanel::onUnseenColorButtonClicked);

    connect(m_bgColorButton, &QPushButton::clicked, this,
            &SidePanel::onBackgroundColorButtonClicked);

    connect(m_pixOrdCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    {
        if (index == 0)
        {
            emit pixelOrderingChanged(PixelOrdering::NESTED);
        }
        else if (index == 1)
        {
            emit pixelOrderingChanged(PixelOrdering::RING);
        }
    });

    connect(m_colormapMinSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &SidePanel::colormapMinChanged);

    connect(m_colormapMaxSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &SidePanel::colormapMaxChanged);

    connect(m_maskOverlayButton, &QPushButton::clicked, this,
            &SidePanel::onLoadMaskButtonClicked);

    connect(m_clearMaskButton, &QPushButton::clicked, this,
            &SidePanel::maskClearRequested);

    connect(m_maskModeCombo, &QComboBox::currentIndexChanged, this,
            [this](int mode)
    {
        updateDimRowVisibility();
        emit maskModeChanged(mode);
    });

    // New HDU: list its columns and load its first one
    connect(m_hduCombo, &QComboBox::activated, this, [this](int)
    {
        const int hdu = m_hduCombo->currentData().toInt();
        fillColumnCombo(hdu, 0);
        emit fitsSelectionChanged(hdu, m_columnCombo->currentData().toInt());
    });
    connect(m_columnCombo, &QComboBox::activated, this, [this](int)
    {
        emit fitsSelectionChanged(m_hduCombo->currentData().toInt(),
                                  m_columnCombo->currentData().toInt());
    });

    connect(m_graticuleCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        updateGraticuleRowVisibility();
        emit graticuleToggled(checked);
    });

    connect(m_graticuleStepCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { emit graticuleStepChanged(graticuleStep()); });

    connect(m_maskOpacitySlider, &QSlider::valueChanged, this,
            [this](int v) { emit maskOpacityChanged(v / 100.0f); });

    connect(m_colormapResetBtn, &QPushButton::clicked, this,
            &SidePanel::colormapRangeResetRequested);

    connect(m_reverseColormapCheck, &QCheckBox::toggled, this,
            &SidePanel::colormapReverseChanged);

    connect(m_colormapScaleCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    {
        if (index == 0)
        {
            emit colormapScaleChanged(ColormapScale::LINEAR);
        }
        else if (index == 1)
        {
            emit colormapScaleChanged(ColormapScale::LOG);
        }
    });
}

void
SidePanel::setPixelOrdering(PixelOrdering ordering)
{
    if (ordering == PixelOrdering::NESTED)
        m_pixOrdCombo->setCurrentIndex(0);
    else if (ordering == PixelOrdering::RING)
        m_pixOrdCombo->setCurrentIndex(1);
}

void
SidePanel::setColormapScale(ColormapScale scale)
{
    QSignalBlocker blocker(m_colormapScaleCombo);
    m_colormapScaleCombo->setCurrentIndex(static_cast<int>(scale));
}

void
SidePanel::setColormapRange(double min, double max)
{
    QSignalBlocker minBlock(m_colormapMinSpin);
    QSignalBlocker maxBlock(m_colormapMaxSpin);
    m_colormapMinSpin->setValue(min);
    m_colormapMaxSpin->setValue(max);
}

QWidget *
SidePanel::pixelOrderingWidget()
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    widget->setLayout(layout);
    m_pixOrdCombo = new QComboBox();
    m_pixOrdCombo->addItem("Nested");
    m_pixOrdCombo->addItem("Ring");

    layout->addRow("Pixel Ordering:", m_pixOrdCombo);
    return widget;
}

// A titled box with a form layout inside, used to group related controls
static QGroupBox *
makeGroup(const QString &title, QFormLayout *&layout)
{
    auto *box = new QGroupBox(title);
    layout    = new QFormLayout(box);
    return box;
}

QWidget *
SidePanel::overlaysWidget()
{
    QWidget *widget = new QWidget();
    auto *outer     = new QVBoxLayout(widget);
    outer->setContentsMargins(0, 0, 0, 0);

    // --- Masks ---
    QFormLayout *layout;
    outer->addWidget(makeGroup("Masks", layout));
    m_overlaysLayout = layout; // used to hide the dim-strength row

    m_maskOverlayButton = new QPushButton("Add Mask...");
    m_clearMaskButton   = new QPushButton("Clear");
    m_maskModeCombo     = new QComboBox();
    m_maskModeCombo->addItems({"Hide", "Dim"});
    m_maskOpacitySlider = new QSlider(Qt::Horizontal);
    m_maskOpacitySlider->setRange(0, 100);
    m_maskOpacitySlider->setValue(80);

    QWidget *buttons = new QWidget();
    QHBoxLayout *row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_maskOverlayButton);
    row->addWidget(m_clearMaskButton);

    // One checkbox row per loaded mask; filled by setMasks()
    m_maskListWidget = new QWidget();
    m_maskListLayout = new QVBoxLayout(m_maskListWidget);
    m_maskListLayout->setContentsMargins(0, 0, 0, 0);
    m_maskListLayout->setSpacing(2);

    layout->addRow(buttons);
    layout->addRow(m_maskListWidget);
    layout->addRow("Mode", m_maskModeCombo);
    layout->addRow("Dim strength", m_maskOpacitySlider);

    setMasks({}, {}, {}, {}, 0, 0.8f);

    m_fskyLabel = new QLabel();
    layout->addRow("Sky fraction", m_fskyLabel);

    // --- Graticule ---
    outer->addWidget(makeGroup("Graticule", m_graticuleLayout));

    m_graticuleCheckBox = new QCheckBox("Show Graticule");
    m_graticuleLayout->addRow(m_graticuleCheckBox);

    m_graticuleStepCombo = new QComboBox();
    for (double deg : {10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 90.0})
        m_graticuleStepCombo->addItem(QString("%1°").arg(deg), deg);
    m_graticuleStepCombo->setCurrentIndex(3); // 30°
    m_graticuleLayout->addRow("Grid spacing", m_graticuleStepCombo);

    m_graticuleColorButton = new QPushButton("");
    m_graticuleLayout->addRow("Color",
                              m_graticuleColorButton = new QPushButton(""));

    updateGraticuleRowVisibility();

    return widget;
}

// The spacing only matters while the graticule is on
void
SidePanel::updateGraticuleRowVisibility()
{
    m_graticuleLayout->setRowVisible(m_graticuleStepCombo,
                                     m_graticuleCheckBox->isChecked());
}

QWidget *
SidePanel::colormapWidget()
{
    QWidget *widget = new QWidget();
    auto *outer     = new QVBoxLayout(widget);
    outer->setContentsMargins(0, 0, 0, 0);

    QFormLayout *layout;

    // --- Colormap ---
    outer->addWidget(makeGroup("Colormap", layout));

    m_colormapCombo = new QComboBox();
    const QStringList colormaps
        = {"Grayscale", "Inferno", "Magma",  "Plasma", "Viridis",
           "Cubehelix", "Jet",     "HSV",    "Hot",    "Cool",
           "Spring",    "Summer",  "Autumn", "Winter", "Rainbow"};
    m_colormapCombo->addItems(colormaps);
    m_colormapCombo->setCurrentIndex(static_cast<int>(Colormap::VIRIDIS));
    layout->addRow("Colormap", m_colormapCombo);

    m_reverseColormapCheck = new QCheckBox("Reverse");
    layout->addRow(m_reverseColormapCheck);

    // --- Range ---
    outer->addWidget(makeGroup("Range", layout));

    m_colormapMinSpin = new QDoubleSpinBox();
    m_colormapMinSpin->setRange(-1e9, 1e9);
    m_colormapMinSpin->setDecimals(6);

    m_colormapMaxSpin = new QDoubleSpinBox();
    m_colormapMaxSpin->setRange(-1e9, 1e9);
    m_colormapMaxSpin->setDecimals(6);

    layout->addRow("Min", m_colormapMinSpin);
    layout->addRow("Max", m_colormapMaxSpin);

    m_colormapScaleCombo = new QComboBox();
    m_colormapScaleCombo->addItem("Linear");
    m_colormapScaleCombo->addItem("Logarithmic");
    layout->addRow("Scale", m_colormapScaleCombo);

    m_colormapResetBtn = new QPushButton("Reset Range");
    layout->addRow(m_colormapResetBtn);

    // --- Colors ---
    outer->addWidget(makeGroup("Colors", layout));

    m_bgColorButton = new QPushButton("");
    m_bgColorButton->setFixedHeight(20);
    m_bgColorButton->setStyleSheet(
        QString("background-color: #%1;").arg(0x0A0A0F, 6, 16, QChar('0')));
    layout->addRow("Background:", m_bgColorButton);

    m_unseenColorButton = new QPushButton("");
    m_unseenColorButton->setFixedHeight(20);
    m_unseenColorButton->setStyleSheet(
        QString("background-color: #%1;").arg(0x000000, 6, 16, QChar('0')));
    layout->addRow("Unseen:", m_unseenColorButton);

    return widget;
}

void
SidePanel::setBackgroundColor(uint32_t color)
{
    m_bgColor = QColor::fromRgb(color & 0xFFFFFF);
    m_bgColorButton->setStyleSheet(
        QString("background-color: #%1;").arg(color, 6, 16, QChar('0')));
}

void
SidePanel::onBackgroundColorButtonClicked()
{

    QColor color = QColorDialog::getColor(QColor(m_bgColor), this,
                                          "Select Background Color");
    if (color.isValid())
    {
        auto bgcolor = color.rgb() & 0xFFFFFF;
        setBackgroundColor(bgcolor);
        emit backgroundColorChanged(bgcolor);
    }
}

void
SidePanel::setUnseenColor(uint32_t color)
{
    m_unseenColor = QColor::fromRgb(color & 0xFFFFFF);
    m_unseenColorButton->setStyleSheet(
        QString("background-color: #%1;").arg(color, 6, 16, QChar('0')));
}

void
SidePanel::onUnseenColorButtonClicked()
{
    QColor color = QColorDialog::getColor(QColor(m_unseenColor), this,
                                          "Select Unseen Color");
    if (color.isValid())
    {
        auto unseencolor = color.rgb() & 0xFFFFFF;
        setUnseenColor(unseencolor);
        emit unseenColorChanged(unseencolor);
    }
}

void
SidePanel::setMasks(const QStringList &names, const QStringList &paths,
                    const QList<bool> &enabled, const QList<bool> &inverted,
                    int mode, float opacity)
{
    // Drop the old rows
    while (QLayoutItem *item = m_maskListLayout->takeAt(0))
    {
        if (QWidget *w = item->widget())
        {
            // May be called from this row's own toggled/clicked signal
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    for (int i = 0; i < names.size(); ++i)
    {
        QWidget *rowWidget = new QWidget();
        QHBoxLayout *row   = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);

        QCheckBox *check = new QCheckBox(names[i]);
        check->setToolTip(paths[i]);
        check->setChecked(enabled[i]);
        connect(check, &QCheckBox::toggled, this,
                [this, i](bool on) { emit maskToggled(i, on); });

        QCheckBox *invert = new QCheckBox("Inv");
        invert->setToolTip("Invert this mask (keep what it masks)");
        invert->setChecked(inverted[i]);
        connect(invert, &QCheckBox::toggled, this,
                [this, i](bool on) { emit maskInvertToggled(i, on); });

        QToolButton *remove = new QToolButton();
        remove->setText(QString::fromUtf8("\u00d7"));
        remove->setAutoRaise(true);
        remove->setToolTip("Remove this mask");
        connect(remove, &QToolButton::clicked, this,
                [this, i]() { emit maskRemoveRequested(i); });

        row->addWidget(check, 1);
        row->addWidget(invert);
        row->addWidget(remove);
        m_maskListLayout->addWidget(rowWidget);
    }

    const bool any = !names.isEmpty();
    QSignalBlocker b1(m_maskModeCombo);
    QSignalBlocker b2(m_maskOpacitySlider);
    m_maskModeCombo->setCurrentIndex(mode);
    m_maskOpacitySlider->setValue(qRound(opacity * 100.0f));

    m_clearMaskButton->setEnabled(any);
    m_maskModeCombo->setEnabled(any);
    m_maskOpacitySlider->setEnabled(any);
    updateDimRowVisibility();
}

// The mode row needs at least one mask; the strength slider (and its label)
// additionally only applies to the "Dim" mode
void
SidePanel::updateDimRowVisibility()
{
    constexpr int kDimMode = 1;
    const bool anyMask     = m_maskListLayout->count() > 0;
    m_overlaysLayout->setRowVisible(m_maskModeCombo, anyMask);
    m_overlaysLayout->setRowVisible(
        m_maskOpacitySlider,
        anyMask && m_maskModeCombo->currentIndex() == kDimMode);
}

void
SidePanel::onLoadMaskButtonClicked()
{
    // TODO: Enable loading multiple masks that cab be toggled on/off in the
    // overlays section
    auto files = QFileDialog::getOpenFileNames(
        this, "Load Mask(s)", "", "FITS Files (*.fits);;All Files (*)");

    if (files.isEmpty())
        return;

    emit maskFilesSelected(files);
}

QWidget *
SidePanel::mapInfoWidget()
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    // Read-only (but selectable) values
    auto makeField = [](QLineEdit *&field)
    {
        field = new QLineEdit();
        field->setReadOnly(true);
        return field;
    };

    layout->addRow("HDU:", m_hduCombo = new QComboBox());
    layout->addRow("Column:", m_columnCombo = new QComboBox());
    layout->addRow("Nside:", makeField(m_nsideEdit));
    layout->addRow("Npix:", makeField(m_npixEdit));
    layout->addRow("Min:", makeField(m_minEdit));
    layout->addRow("Max:", makeField(m_maxEdit));
    layout->addRow("Average:", makeField(m_avgEdit));
    layout->addRow("RMS:", makeField(m_rmsEdit));
    layout->addRow("Std dev:", makeField(m_stdEdit));

    return widget;
}

void
SidePanel::setMapInfo(qlonglong nside, qlonglong npix, double avg, double rms,
                      double std, double min, double max)
{
    m_nsideEdit->setText(QString::number(nside));
    m_npixEdit->setText(QString::number(npix));
    m_minEdit->setText(QString::number(min, 'g', 6));
    m_maxEdit->setText(QString::number(max, 'g', 6));
    m_avgEdit->setText(QString::number(avg, 'g', 6));
    m_rmsEdit->setText(QString::number(rms, 'g', 6));
    m_stdEdit->setText(QString::number(std, 'g', 6));
}

// Fills the HDU and column combos from the file's table HDUs. Items carry the
// real (1-based) HDU / column numbers as data.
void
SidePanel::setFitsInfo(const FitsInfo &info, int hdu, int column)
{
    QSignalBlocker b1(m_hduCombo);
    QSignalBlocker b2(m_columnCombo);

    m_fitsInfo = info;
    m_hduCombo->clear();
    for (const FitsHdu &h : info.hdus)
    {
        const QString label = h.name.isEmpty()
                                  ? QString::number(h.index)
                                  : QString("%1: %2").arg(h.index).arg(h.name);
        m_hduCombo->addItem(label, h.index);
    }
    m_hduCombo->setEnabled(info.hdus.size() > 1);

    const int row = m_hduCombo->findData(hdu);
    if (row >= 0)
        m_hduCombo->setCurrentIndex(row);
    fillColumnCombo(hdu, column);
}

void
SidePanel::fillColumnCombo(int hdu, int column)
{
    QSignalBlocker blocker(m_columnCombo);
    m_columnCombo->clear();
    if (const FitsHdu *h = m_fitsInfo.find(hdu))
        for (const FitsColumn &c : h->columns)
            m_columnCombo->addItem(
                c.name.isEmpty() ? QString::number(c.index) : c.name, c.index);
    m_columnCombo->setEnabled(m_columnCombo->count() > 1);

    const int row = m_columnCombo->findData(column);
    if (row >= 0)
        m_columnCombo->setCurrentIndex(row);
}

void
SidePanel::onGraticuleColorButtonClicked()
{
    QColor color = QColorDialog::getColor(QColor(m_graticuleColor), this,
                                          "Select Graticule Color");
    if (color.isValid())
    {
        auto graticuleColor = color.rgb() & 0xFFFFFF;
        m_graticuleColor    = QColor::fromRgb(graticuleColor);
        m_graticuleColorButton->setStyleSheet(
            QString("background-color: #%1;")
                .arg(graticuleColor, 6, 16, QChar('0')));
        emit graticuleColorChanged(graticuleColor);
    }
}

QWidget *
SidePanel::projectionWidget()
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_projectionCombo = new QComboBox();
    m_projectionCombo->addItem("Mollweide",
                               static_cast<int>(Projection::MOLLWEIDE));
    m_projectionCombo->addItem("Plate Carrée",
                               static_cast<int>(Projection::PLATE_CARREE));
    m_projectionCombo->addItem("Orthographic",
                               static_cast<int>(Projection::ORTHOGRAPHIC));
    m_projectionCombo->addItem(
        "Azimuthal Equidistant",
        static_cast<int>(Projection::AZIMUTHAL_EQUIDISTANT));
    m_projectionCombo->addItem("Gnomonic",
                               static_cast<int>(Projection::GNOMONIC));

    layout->addRow("Projection:", m_projectionCombo);

    connect(m_projectionCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    {
        emit projectionChanged(static_cast<Projection>(
            m_projectionCombo->itemData(index).toInt()));
    });

    // Orientation of the sky in the view. Dragging in the view updates these
    auto makeSpin = [this](double min, double max, bool wrap)
    {
        auto *spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setDecimals(2);
        spin->setSingleStep(1.0);
        spin->setSuffix(QString::fromUtf8("\u00b0"));
        spin->setWrapping(wrap);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double)
        {
            emit rotationEdited(m_centerLonSpin->value(),
                                m_centerLatSpin->value(), m_rollSpin->value());
        });
        return spin;
    };
    m_centerLonSpin = makeSpin(-180.0, 180.0, true);
    m_centerLatSpin = makeSpin(-90.0, 90.0, false);
    m_rollSpin      = makeSpin(-180.0, 180.0, true);
    layout->addRow("Center longitude:", m_centerLonSpin);
    layout->addRow("Center latitude:", m_centerLatSpin);
    layout->addRow("Roll:", m_rollSpin);

    auto *reset = new QPushButton("Reset Rotation");
    connect(reset, &QPushButton::clicked, this,
            &SidePanel::rotationResetRequested);
    layout->addRow(reset);

    return widget;
}
