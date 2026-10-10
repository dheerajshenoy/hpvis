#include "sidepanel.hpp"

#include <QFormLayout>

SidePanel::SidePanel(QWidget *parent) : QDockWidget(parent)
{
    setupUI();
    setupConnections();
}

void
SidePanel::setupUI()
{
    QWidget *widget = new QWidget();
    setMinimumWidth(200);
    QVBoxLayout *layout = new QVBoxLayout();

    m_pixOrdCombo = new QComboBox();
    m_pixOrdCombo->addItem("Nested");
    m_pixOrdCombo->addItem("Ring");

    QFormLayout *formLayout = new QFormLayout();

    formLayout->addRow("Pixel Ordering:", m_pixOrdCombo);
    layout->addLayout(formLayout);

    widget->setLayout(layout);
    setWidget(widget);
}

void
SidePanel::setupConnections()
{
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
}

void
SidePanel::setPixelOrdering(PixelOrdering ordering)
{
    if (ordering == PixelOrdering::NESTED)
        m_pixOrdCombo->setCurrentIndex(0);
    else if (ordering == PixelOrdering::RING)
        m_pixOrdCombo->setCurrentIndex(1);
}
