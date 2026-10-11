#include "viewtoolbar.hpp"

#include <QLabel>

ViewToolBar::ViewToolBar(QWidget *parent) : QToolBar("View", parent)
{
    setMovable(false);
    setFloatable(false);

    auto addLabeled = [this](const QString &text, QWidget *w)
    {
        auto *label = new QLabel(text + " ");
        addWidget(label);
        addWidget(w);
    };

    // --- Data selection ---
    m_hduCombo    = new QComboBox();
    m_columnCombo = new QComboBox();
    addLabeled("HDU", m_hduCombo);
    addLabeled("Column", m_columnCombo);
    addSeparator();

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

    // --- Projection ---
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
    addLabeled("Projection", m_projectionCombo);
    connect(m_projectionCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    {
        emit projectionChanged(static_cast<Projection>(
            m_projectionCombo->itemData(index).toInt()));
    });
    addSeparator();

    // --- Colormap ---
    m_colormapCombo = new QComboBox();
    m_colormapCombo->addItems({"Grayscale", "Inferno", "Magma",  "Plasma",
                               "Viridis",   "Cubehelix", "Jet",  "HSV",
                               "Hot",       "Cool",    "Spring", "Summer",
                               "Autumn",    "Winter",  "Rainbow"});
    m_colormapCombo->setCurrentIndex(static_cast<int>(Colormap::VIRIDIS));
    addLabeled("Colormap", m_colormapCombo);
    connect(m_colormapCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    { emit colormapChanged(static_cast<Colormap>(index)); });
}

void
ViewToolBar::setColormap(Colormap colormap)
{
    QSignalBlocker blocker(m_colormapCombo);
    m_colormapCombo->setCurrentIndex(static_cast<int>(colormap));
}

void
ViewToolBar::setProjection(Projection projection)
{
    QSignalBlocker blocker(m_projectionCombo);
    const int i = m_projectionCombo->findData(static_cast<int>(projection));
    if (i >= 0)
        m_projectionCombo->setCurrentIndex(i);
}

// Items carry the real (1-based) HDU / column numbers as data
void
ViewToolBar::setFitsInfo(const FitsInfo &info, int hdu, int column)
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
ViewToolBar::fillColumnCombo(int hdu, int column)
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
