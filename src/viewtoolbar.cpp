#include "viewtoolbar.hpp"

#include "colormapregistry.hpp"

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
    m_projectionCombo->addItem("Equirectangular",
                               static_cast<int>(Projection::EQUIRECTANGULAR));
    m_projectionCombo->addItem("Orthographic",
                               static_cast<int>(Projection::ORTHOGRAPHIC));
    m_projectionCombo->addItem(
        "Azimuthal Equidistant",
        static_cast<int>(Projection::AZIMUTHAL_EQUIDISTANT));
    m_projectionCombo->addItem("Gnomonic",
                               static_cast<int>(Projection::GNOMONIC));
    m_projectionCombo->addItem("Mercator",
                               static_cast<int>(Projection::MERCATOR));
    m_projectionCombo->addItem("Sinusoidal",
                               static_cast<int>(Projection::SINUSOIDAL));
    m_projectionCombo->addItem(
        "Lambert Cylindrical",
        static_cast<int>(Projection::LAMBERT_CYLINDRICAL));
    m_projectionCombo->addItem("Gall-Peters",
                               static_cast<int>(Projection::GALL_PETERS));
    m_projectionCombo->addItem("Stereographic",
                               static_cast<int>(Projection::STEREOGRAPHIC));
    m_projectionCombo->addItem("Hammer",
                               static_cast<int>(Projection::HAMMER));
    addLabeled("Projection", m_projectionCombo);

    // --- Orientation ---
    m_orientationCombo = new QComboBox();
    m_orientationCombo->addItems(
        {"Free", "Galactic", "Ecliptic", "Equatorial"});
    m_orientationCombo->setToolTip(
        "Free: the map's own frame, dragging also rolls the view.\n"
        "Galactic, Ecliptic, Equatorial: show the sky in that frame, north "
        "up.");
    addLabeled("Orientation", m_orientationCombo);
    connect(m_orientationCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) { emit orientationChanged(Orientation(index)); });
    connect(m_projectionCombo, &QComboBox::currentIndexChanged, this,
            [this](int index)
    {
        emit projectionChanged(static_cast<Projection>(
            m_projectionCombo->itemData(index).toInt()));
    });
    addSeparator();

    // --- Colormap ---
    m_colormapCombo = new QComboBox();
    m_colormapCombo->addItems(ColormapRegistry::instance().names());
    m_colormapCombo->setCurrentIndex(static_cast<int>(Colormap::VIRIDIS));
    addLabeled("Colormap", m_colormapCombo);
    // Custom colormaps added or loaded at run time
    connect(&ColormapRegistry::instance(), &ColormapRegistry::changed, this,
            [this]()
    {
        QSignalBlocker blocker(m_colormapCombo);
        const int current = m_colormapCombo->currentIndex();
        m_colormapCombo->clear();
        m_colormapCombo->addItems(ColormapRegistry::instance().names());
        m_colormapCombo->setCurrentIndex(current);
    });
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

void
ViewToolBar::setOrientation(Orientation orientation)
{
    QSignalBlocker blocker(m_orientationCombo);
    m_orientationCombo->setCurrentIndex(int(orientation));
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
