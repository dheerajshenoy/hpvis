#include "sidepanel.hpp"

#include "collapsiblesection.hpp"

#include <QFormLayout>
#include <QScrollArea>

SidePanel::SidePanel(QWidget *parent) : QDockWidget(parent)
{
    setupUI();
    setupConnections();
}

void
SidePanel::setupUI()
{
    setMinimumWidth(220);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *container  = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    layout->addWidget(
        new CollapsibleSection("Pixel Ordering", pixelOrderingWidget()));
    layout->addWidget(new CollapsibleSection("Color", colormapWidget()));
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
SidePanel::colormapWidget()
{
    QWidget *widget     = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    widget->setLayout(layout);

    m_colormapCombo = new QComboBox();

    const QStringList colormaps
        = {"Grayscale", "Inferno", "Magma",  "Plasma", "Viridis",
           "Cubehelix", "Jet",     "HSV",    "Hot",    "Cool",
           "Spring",    "Summer",  "Autumn", "Winter"};

    m_colormapCombo->addItems(colormaps);
    m_colormapCombo->setCurrentIndex(static_cast<int>(Colormap::VIRIDIS));
    layout->addRow("Colormap", m_colormapCombo);

    m_colormapMinSpin = new QDoubleSpinBox();
    m_colormapMinSpin->setRange(-1000000.0, 1000000.0);

    m_colormapMaxSpin = new QDoubleSpinBox();
    m_colormapMaxSpin->setRange(-1000000.0, 1000000.0);

    layout->addRow("Colormap Min", m_colormapMinSpin);
    layout->addRow("Colormap Max", m_colormapMaxSpin);

    m_colormapScaleCombo = new QComboBox();

    m_colormapScaleCombo->addItem("Linear");
    m_colormapScaleCombo->addItem("Logarithmic");

    layout->addRow("Colormap Scale", m_colormapScaleCombo);

    widget->setLayout(layout);
    return widget;
}
