#include "sidepanel.hpp"

#include "collapsiblesection.hpp"

#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <qpushbutton.h>

SidePanel::SidePanel(QWidget *parent) : QDockWidget(parent)
{
    setupUI();
    setupConnections();
}

void
SidePanel::setupUI()
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
    layout->addStretch();

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(container);
    setWidget(scrollArea);
}

void
SidePanel::setupConnections()
{
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

QWidget *
SidePanel::overlaysWidget()
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);
    m_overlaysLayout    = layout;

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
    widget->setLayout(layout);
    return widget;
}

QWidget *
SidePanel::colormapWidget()
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_bgColorButton = new QPushButton("");
    m_bgColorButton->setFixedHeight(20);
    m_bgColorButton->setStyleSheet(
        QString("background-color: #%1;").arg(0x0A0A0F, 6, 16, QChar('0')));

    layout->addRow("Background Color:", m_bgColorButton);

    m_unseenColorButton = new QPushButton("");
    m_unseenColorButton->setFixedHeight(20);
    m_unseenColorButton->setStyleSheet(
        QString("background-color: #%1;").arg(0x000000, 6, 16, QChar('0')));

    layout->addRow("Unseen Color:", m_unseenColorButton);

    widget->setLayout(layout);

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

    widget->setLayout(layout);
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

// The strength slider (and its label) only applies to the "Dim" mode
void
SidePanel::updateDimRowVisibility()
{
    constexpr int kDimMode = 1;
    m_overlaysLayout->setRowVisible(
        m_maskOpacitySlider, m_maskModeCombo->currentIndex() == kDimMode);
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

    layout->addRow("Nside:", makeField(m_nsideEdit));
    layout->addRow("Npix:", makeField(m_npixEdit));
    layout->addRow("Average:", makeField(m_avgEdit));
    layout->addRow("RMS:", makeField(m_rmsEdit));
    layout->addRow("Std dev:", makeField(m_stdEdit));

    return widget;
}

void
SidePanel::setMapInfo(qlonglong nside, qlonglong npix, double avg, double rms,
                      double std)
{
    m_nsideEdit->setText(QString::number(nside));
    m_npixEdit->setText(QString::number(npix));
    m_avgEdit->setText(QString::number(avg, 'g', 6));
    m_rmsEdit->setText(QString::number(rms, 'g', 6));
    m_stdEdit->setText(QString::number(std, 'g', 6));
}
