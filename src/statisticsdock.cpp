#include "statisticsdock.hpp"

#include "appsettings.hpp"
#include "view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QQuickItem>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QQuickView>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent>
#include <cmath>

namespace
{
enum Row
{
    RowNside,
    RowNpix,
    RowValid,
    RowFsky,
    RowArea,
    RowMin,
    RowMax,
    RowAverage,
    RowRms,
    RowStd,
    kRowCount
};
} // namespace

StatisticsDock::StatisticsDock(QWidget *parent)
    : QDockWidget("Statistics", parent)
{
    setObjectName("StatisticsDock");

    // One row per statistic, a column for the whole map and one for the
    // region picked on it
    m_table = new QTableWidget(kRowCount, 3);
    m_table->setHorizontalHeaderLabels({"Statistic", "Whole map", "Region"});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    static const char *const names[kRowCount]
        = {"Nside",   "Npix",    "Valid pixels", "Sky fraction", "Area",
           "Min",     "Max",     "Average",      "RMS",          "Std dev"};
    for (int r = 0; r < kRowCount; ++r)
    {
        m_table->setItem(r, 0, new QTableWidgetItem(names[r]));
        for (int c = 1; c < 3; ++c)
        {
            auto *item = new QTableWidgetItem("-");
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, c, item);
        }
    }
    m_table->resizeRowsToContents();
    int height = m_table->horizontalHeader()->height()
                 + 2 * m_table->frameWidth();
    for (int r = 0; r < kRowCount; ++r)
        height += m_table->rowHeight(r);
    m_table->setFixedHeight(height);

    m_regionHint = new QLabel("Region: Ctrl + Shift + drag on the map, or "
                              "View > Select Statistics Region.");
    m_regionHint->setWordWrap(true);
    m_regionClear = new QToolButton();
    m_regionClear->setText("Clear region");
    connect(m_regionClear, &QToolButton::clicked, this, [this]()
    {
        if (m_view)
            m_view->glWidget()->clearStatsRegion();
    });
    auto *copy = new QToolButton();
    copy->setText("Copy table");
    connect(copy, &QToolButton::clicked, this, [this]() { copyTable(); });

    auto *buttons = new QHBoxLayout();
    buttons->addWidget(m_regionHint, 1);
    buttons->addWidget(m_regionClear);
    buttons->addWidget(copy);

    auto *whole = new QWidget();
    auto *outer = new QVBoxLayout(whole);
    outer->addWidget(m_table);
    outer->addLayout(buttons);
    updateTable();
    initHistogramUI(whole);
    setWidget(whole);

    // Redraw (after a pause) when the options change quickly, e.g. the bins
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this,
            &StatisticsDock::updateHistogram);

    // Nothing is computed while the dock is hidden; catch up when it shows
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible)
    {
        if (visible && m_dirty)
            scheduleUpdate();
    });
}

void
StatisticsDock::initHistogramUI(QWidget *parent)
{
    auto *layout = qobject_cast<QVBoxLayout *>(parent->layout());

    const AppSettings colors = AppSettings::load();
    m_plotBackground         = colors.plotBackground;
    m_plotForeground         = colors.plotForeground;
    m_plotLine               = colors.plotLine;

    auto *box     = new QGroupBox("Histogram");
    auto *boxForm = new QVBoxLayout(box);

    // Field and bins
    m_fieldCombo = new QComboBox();
    m_fieldCombo->setToolTip("The field (column) of the map to show");
    m_binsSpin = new QSpinBox();
    m_binsSpin->setRange(2, 5000);
    m_binsSpin->setValue(QSettings().value("histogram/bins", 100).toInt());
    m_binsSpin->setToolTip("Number of bins");

    auto *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("Field:"));
    row1->addWidget(m_fieldCombo, 1);
    row1->addWidget(new QLabel("Bins:"));
    row1->addWidget(m_binsSpin);
    boxForm->addLayout(row1);

    // Options
    QSettings settings;
    m_densityCheck = new QCheckBox("Density");
    m_densityCheck->setToolTip("Divide by the number of pixels and the bin "
                               "width, so maps of different sizes compare");
    m_densityCheck->setChecked(
        settings.value("histogram/density", true).toBool());
    m_logCheck = new QCheckBox("Log y");
    m_logCheck->setChecked(settings.value("histogram/log", false).toBool());
    m_maskCheck = new QCheckBox("Use masks");
    m_maskCheck->setToolTip("Leave out the pixels the enabled masks remove");
    m_maskCheck->setChecked(settings.value("histogram/masks", true).toBool());
    m_clipCheck = new QCheckBox("Clip outliers");
    m_clipCheck->setToolTip("Use the range from the 0.1 % to the 99.9 % "
                            "quantile, leaving out the extreme pixels");
    m_clipCheck->setChecked(settings.value("histogram/clip", false).toBool());

    m_regionCheck = new QCheckBox("Region");
    m_regionCheck->setToolTip("Also plot the pixels inside the selected "
                              "region (Ctrl + Shift + drag on the map)");
    m_regionCheck->setChecked(settings.value("histogram/region", true).toBool());

    auto *row2 = new QHBoxLayout();
    row2->addWidget(m_densityCheck);
    row2->addWidget(m_logCheck);
    row2->addWidget(m_maskCheck);
    row2->addWidget(m_clipCheck);
    row2->addWidget(m_regionCheck);
    row2->addStretch();
    boxForm->addLayout(row2);

    // Comparison and export
    m_compareButton = new QToolButton();
    m_compareButton->setText("Compare");
    m_compareButton->setPopupMode(QToolButton::InstantPopup);
    auto *compareMenu = new QMenu(m_compareButton);
    connect(compareMenu, &QMenu::aboutToShow, this,
            &StatisticsDock::buildCompareMenu);
    m_compareButton->setMenu(compareMenu);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Overlay", 0);
    m_modeCombo->addItem("Ratio to this map", 1);
    m_modeCombo->addItem("Difference from this map", 2);
    m_modeCombo->setCurrentIndex(std::max(
        0, m_modeCombo->findData(settings.value("histogram/mode", 0).toInt())));

    m_exportButton = new QToolButton();
    m_exportButton->setText("Export");
    m_exportButton->setPopupMode(QToolButton::InstantPopup);
    auto *exportMenu = new QMenu(m_exportButton);
    exportMenu->addAction("Plot as &image...", this,
                          [this]() { exportImage(); });
    exportMenu->addAction("Histogram as &CSV...", this,
                          [this]() { exportCsv(); });
    m_exportButton->setMenu(exportMenu);

    auto *row3 = new QHBoxLayout();
    row3->addWidget(m_compareButton);
    row3->addWidget(m_modeCombo);
    row3->addStretch();
    row3->addWidget(m_exportButton);
    boxForm->addLayout(row3);

    m_histStatus = new QLabel();
    m_histStatus->setWordWrap(true);
    m_histStatus->setAlignment(Qt::AlignCenter);
    boxForm->addWidget(m_histStatus);

    m_histView = new QQuickView();
    m_histView->setColor(m_plotBackground);
    m_histView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_histView->setSource(QUrl("qrc:/src/qml/histogram.qml"));
    m_histContainer = QWidget::createWindowContainer(m_histView);
    m_histContainer->setMinimumSize(320, 220);
    boxForm->addWidget(m_histContainer, 1);

    layout->addWidget(box, 1);

    // Any option change redraws; the persistent ones are remembered
    auto changed = [this]()
    {
        QSettings s;
        s.setValue("histogram/bins", m_binsSpin->value());
        s.setValue("histogram/density", m_densityCheck->isChecked());
        s.setValue("histogram/log", m_logCheck->isChecked());
        s.setValue("histogram/masks", m_maskCheck->isChecked());
        s.setValue("histogram/clip", m_clipCheck->isChecked());
        s.setValue("histogram/region", m_regionCheck->isChecked());
        s.setValue("histogram/mode", m_modeCombo->currentData());
    };
    // Bins, field, masks and clipping change the numbers; density, log and the
    // mode only how they are shown
    connect(m_binsSpin, &QSpinBox::valueChanged, this, [this, changed](int)
    {
        changed();
        scheduleUpdate();
    });
    connect(m_fieldCombo, &QComboBox::activated, this,
            [this](int) { scheduleUpdate(); });
    for (QCheckBox *c : {m_maskCheck, m_clipCheck, m_regionCheck})
        connect(c, &QCheckBox::toggled, this, [this, changed](bool)
        {
            changed();
            scheduleUpdate();
        });
    for (QCheckBox *c : {m_densityCheck, m_logCheck})
        connect(c, &QCheckBox::toggled, this, [this, changed](bool)
        {
            changed();
            showResult(false);
        });
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this,
            [this, changed](int)
    {
        changed();
        showResult(false);
    });
}

// The fields are the columns of the map's HDU
void
StatisticsDock::fillFields()
{
    QSignalBlocker block(m_fieldCombo);
    m_fieldCombo->clear();

    const View *view = m_view.data();
    if (!view)
        return;
    const FitsHdu *hdu
        = view->isComputed() ? nullptr : view->fitsInfo().find(view->hdu());
    if (hdu && !hdu->columns.isEmpty())
    {
        for (const FitsColumn &c : hdu->columns)
            m_fieldCombo->addItem(
                c.name.isEmpty() ? QString::number(c.index) : c.name, c.index);
        const int i = m_fieldCombo->findData(view->column());
        m_fieldCombo->setCurrentIndex(i >= 0 ? i : 0);
    }
    else
        m_fieldCombo->addItem("map", 0);
    m_fieldCombo->setEnabled(m_fieldCombo->count() > 1);
}

QString
StatisticsDock::titleOf(const View *view) const
{
    if (m_openMaps)
        for (const OpenMap &m : m_openMaps())
            if (m.view.data() == view)
                return m.title;
    return view ? view->displayName() : QString("map");
}

bool
StatisticsDock::histogramWanted() const
{
    return isVisible() && m_view && m_view->healpixMap();
}

void
StatisticsDock::scheduleUpdate()
{
    if (!histogramWanted())
    {
        m_dirty = true;
        return;
    }
    m_debounce->start();
}

void
StatisticsDock::setView(View *view)
{
    m_view = view;

    const OpenGLWidget *gl = view ? view->glWidget() : nullptr;
    if (!gl || !gl->healpixMap())
    {
        updateTable();
        fillFields();
        m_result = HistogramResult();
        m_histStatus->setText("Open a map to see its histogram.");
        m_histStatus->setVisible(true);
        showResult(false);
        return;
    }

    updateTable();
    fillFields();
    scheduleUpdate();
}

void
StatisticsDock::updateRegion()
{
    updateTable();
    // The histogram follows the region itself, not its numbers
    if (m_view && m_regionCheck->isChecked()
        && m_view->glWidget()->regionRevision() != m_regionRevision)
        scheduleUpdate();
}

void
StatisticsDock::updateTable()
{
    const OpenGLWidget *gl = m_view ? m_view->glWidget() : nullptr;
    const bool hasMap      = gl && gl->healpixMap();
    const bool hasRegion   = hasMap && gl->hasStatsRegion();

    auto num = [](double v)
    {
        return QString::number(v, 'g', 6);
    };
    auto set = [this](int row, int col, const QString &text)
    {
        m_table->item(row, col)->setText(text);
    };
    for (int r = 0; r < kRowCount; ++r)
        for (int c = 1; c < 3; ++c)
            set(r, c, "-");

    constexpr double kSkyDeg2
        = 4.0 * M_PI * (180.0 / M_PI) * (180.0 / M_PI);

    if (hasMap)
    {
        const long npix = gl->healpixMap()->Npix();
        const bool any  = gl->fsky() > 0.0f;
        set(RowNside, 1, QString::number(gl->healpixMap()->Nside()));
        set(RowNpix, 1, QString::number(npix));
        set(RowValid, 1,
            QString::number(std::lround(double(gl->fsky()) * double(npix))));
        set(RowFsky, 1, QString::number(gl->fsky(), 'f', 6));
        set(RowArea, 1,
            QString("%1 deg\u00b2").arg(gl->fsky() * kSkyDeg2, 0, 'g', 6));
        if (any)
        {
            set(RowMin, 1, num(gl->mapMin()));
            set(RowMax, 1, num(gl->mapMax()));
            set(RowAverage, 1, num(gl->mapAverage()));
            set(RowRms, 1, num(gl->mapRms()));
            set(RowStd, 1, num(gl->mapStd()));
        }
    }

    if (hasRegion && gl->regionBusy())
    {
        for (int r = 0; r < kRowCount; ++r)
            if (r != RowNside)
                set(r, 2, "computing...");
    }
    else if (hasRegion)
    {
        const OpenGLWidget::RegionStats &s = gl->regionStats();
        // Valid pixels, fraction and area are those of the valid pixels, as
        // for the whole map; the region itself is s.pixels
        const double validFrac
            = double(s.valid) / double(gl->healpixMap()->Npix());
        set(RowNpix, 2, QString::number(s.pixels));
        set(RowValid, 2, QString::number(s.valid));
        set(RowFsky, 2, QString::number(validFrac, 'f', 6));
        set(RowArea, 2,
            QString("%1 deg\u00b2").arg(validFrac * kSkyDeg2, 0, 'g', 6));
        if (s.valid > 0)
        {
            set(RowMin, 2, num(s.min));
            set(RowMax, 2, num(s.max));
            set(RowAverage, 2, num(s.average));
            set(RowRms, 2, num(s.rms));
            set(RowStd, 2, num(s.std));
        }
    }

    m_regionHint->setVisible(!hasRegion);
    m_regionClear->setVisible(hasRegion);
}

void
StatisticsDock::copyTable()
{
    QString text;
    QTextStream out(&text);
    out << "Statistic\tWhole map\tRegion\n";
    for (int r = 0; r < kRowCount; ++r)
        out << m_table->item(r, 0)->text() << '\t' << m_table->item(r, 1)->text()
            << '\t' << m_table->item(r, 2)->text() << '\n';
    QGuiApplication::clipboard()->setText(text);
}

void
StatisticsDock::forget(const View *view)
{
    m_compareViews.remove(view);
    if (m_view.data() == view)
        m_view = nullptr;
}

void
StatisticsDock::setPlotColors(const QColor &background,
                              const QColor &foreground, const QColor &line)
{
    m_plotBackground = background;
    m_plotForeground = foreground;
    m_plotLine       = line;
    m_histView->setColor(background);
    if (QQuickItem *root = m_histView->rootObject())
    {
        root->setProperty("bgColor", background);
        root->setProperty("fgColor", foreground);
        root->setProperty("lineColor", line);
    }
    // The first curve uses the line color
    if (!m_curveColors.isEmpty())
        m_curveColors[0] = line;
    showResult(true);
}

// Reads the maps (from the files, off the GUI thread) and bins them
void
StatisticsDock::updateHistogram()
{
    if (!histogramWanted())
    {
        m_dirty = true;
        return;
    }
    if (m_running)
    {
        m_rerun = true;
        return;
    }
    m_dirty = false;
    m_rerun = false;

    // A source for a map: its values come from the file (so a 0 / 1 map keeps
    // its zeros) or, for a computed map, from memory
    const bool masks = m_maskCheck->isChecked();
    auto makeSource  = [masks](View *v, int column, const QString &name)
    {
        HistogramSource s;
        s.name           = name;
        OpenGLWidget *gl = v->glWidget();
        // (an overridden pixel ordering is not in the file: use the map in
        // memory, which the masks are indexed like)
        if (v->isComputed() || v->filePath().isEmpty()
            || gl->orderingOverridden())
            s.map
                = std::make_shared<const Healpix_Map<float>>(*gl->healpixMap());
        else
        {
            s.path   = v->filePath();
            s.hdu    = v->hdu();
            s.column = column > 0 ? column : v->column();
        }
        if (masks)
        {
            std::vector<uint8_t> keep = gl->keepMask();
            if (!keep.empty())
                s.keep = std::make_shared<const std::vector<uint8_t>>(
                    std::move(keep));
        }
        return s;
    };

    static const QStringList palette
        = {"#e69f00", "#009e73", "#cc79a7", "#d55e00",
           "#f0e442", "#0072b2", "#999999", "#56b4e9"};

    std::vector<HistogramSource> sources;
    QList<QColor> colors;
    View *current = m_view.data();
    QString name  = titleOf(current);
    if (m_fieldCombo->count() > 1)
        name += " (" + m_fieldCombo->currentText() + ")";
    sources.push_back(
        makeSource(current, m_fieldCombo->currentData().toInt(), name));
    colors << m_plotLine;

    // The region of the current map, next to it in the same plot
    m_regionRevision = current->glWidget()->regionRevision();
    if (m_regionCheck->isChecked())
    {
        auto builder = current->glWidget()->regionKeepBuilder(masks);
        if (builder)
        {
            HistogramSource region = sources.front();
            region.name += " [region]";
            region.keep.reset();
            region.optional = true;
            region.makeKeep = std::move(builder);
            sources.push_back(std::move(region));
            colors << QColor(80, 200, 255); // the outline's color on the map
        }
    }

    int next = 0;
    for (const OpenMap &m : m_openMaps ? m_openMaps() : QList<OpenMap>())
    {
        if (!m.view || m.view.data() == current
            || !m_compareViews.contains(m.view.data()) || !m.view->healpixMap())
            continue;
        sources.push_back(makeSource(m.view.data(), 0, m.title));
        QColor c(palette[next++ % palette.size()]);
        if (c == m_plotLine)
            c = QColor(palette[next++ % palette.size()]);
        colors << c;
    }

    const int bins  = m_binsSpin->value();
    const bool clip = m_clipCheck->isChecked();
    const int gen   = ++m_generation;
    m_running       = true;
    m_histStatus->setText("Computing the histogram…");
    m_histStatus->setVisible(true);

    auto *watcher = new QFutureWatcher<HistogramResult>(this);
    connect(watcher, &QFutureWatcher<HistogramResult>::finished, this,
            [this, watcher, gen, colors]()
    {
        HistogramResult r = watcher->result();
        watcher->deleteLater();
        m_running = false;

        if (gen == m_generation)
        {
            if (!r.error.isEmpty())
            {
                m_result = HistogramResult();
                m_histStatus->setText("Histogram: " + r.error);
                m_histStatus->setVisible(true);
                showResult(false);
            }
            else
            {
                m_result         = std::move(r);
                m_curveColors    = colors;
                m_curveColors[0] = m_plotLine;
                m_histStatus->setVisible(false);
                showResult(false);
            }
        }
        if (m_rerun)
            scheduleUpdate();
    });
    watcher->setFuture(QtConcurrent::run([sources, bins, clip]()
    { return computeHistograms(sources, bins, clip); }));
}

// Draws the last result: counts or density, overlaid or as a ratio /
// difference to the current map
void
StatisticsDock::showResult(bool keepRange)
{
    QQuickItem *root = m_histView->rootObject();
    if (!root)
        return;
    root->setProperty("bgColor", m_plotBackground);
    root->setProperty("fgColor", m_plotForeground);
    root->setProperty("lineColor", m_plotLine);

    const int bins = m_result.bins;
    if (m_result.curves.empty() || bins <= 0)
    {
        QMetaObject::invokeMethod(
            root, "setHistogram", Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 1.0),
            Q_ARG(QVariant, 1), Q_ARG(QVariant, QVariantList()),
            Q_ARG(QVariant, QString("Pixel value")), Q_ARG(QVariant, QString()),
            Q_ARG(QVariant, m_logCheck->isChecked()), Q_ARG(QVariant, false));
        return;
    }

    const double bw    = (m_result.hi - m_result.lo) / bins;
    const bool density = m_densityCheck->isChecked();
    int mode           = m_modeCombo->currentData().toInt();
    if (m_result.curves.size() < 2)
        mode = 0;

    auto values = [&](const HistogramCurve &c)
    {
        std::vector<double> v(bins);
        const double norm = density ? 1.0 / (std::max(1L, c.valid) * bw) : 1.0;
        for (int i = 0; i < bins; ++i)
            v[i] = c.counts[i] * norm;
        return v;
    };
    auto toList = [](const std::vector<double> &v)
    {
        QVariantList l;
        l.reserve(int(v.size()));
        for (double x : v)
            l.append(x);
        return l;
    };

    const std::vector<double> ref = values(m_result.curves[0]);
    QVariantList curves;
    for (size_t k = (mode == 0 ? 0 : 1); k < m_result.curves.size(); ++k)
    {
        std::vector<double> v = values(m_result.curves[k]);
        if (mode == 1)
            for (int i = 0; i < bins; ++i)
                v[i] = ref[i] != 0.0 ? v[i] / ref[i] : std::nan("");
        else if (mode == 2)
            for (int i = 0; i < bins; ++i)
                v[i] -= ref[i];

        QVariantMap curve;
        curve["name"] = m_result.curves[k].name;
        curve["color"]
            = (k < size_t(m_curveColors.size()) ? m_curveColors[int(k)]
                                                : m_plotLine)
                  .name();
        curve["values"] = toList(v);
        curves.append(curve);
    }

    const QString unit = density ? "density" : "pixels per bin";
    QString yTitle;
    if (mode == 1)
        yTitle = "Ratio to " + m_result.curves[0].name;
    else if (mode == 2)
        yTitle = QString("Difference from %1 (%2)")
                     .arg(m_result.curves[0].name, unit);
    else
        yTitle = density ? "Density (1 / pixel value)" : "Pixels per bin";

    QMetaObject::invokeMethod(
        root, "setHistogram", Q_ARG(QVariant, m_result.lo), Q_ARG(QVariant, bw),
        Q_ARG(QVariant, bins), Q_ARG(QVariant, curves),
        Q_ARG(QVariant, QString("Pixel value")), Q_ARG(QVariant, yTitle),
        Q_ARG(QVariant, m_logCheck->isChecked()), Q_ARG(QVariant, keepRange));
}

// The Compare menu: the other open maps
void
StatisticsDock::buildCompareMenu()
{
    auto *menu = qobject_cast<QMenu *>(m_compareButton->menu());
    menu->clear();

    int shown = 0;
    for (const OpenMap &m : m_openMaps ? m_openMaps() : QList<OpenMap>())
    {
        if (!m.view || m.view.data() == m_view.data() || !m.view->healpixMap())
            continue;
        const View *v = m.view.data();
        QAction *a    = menu->addAction(m.title);
        a->setCheckable(true);
        a->setChecked(m_compareViews.contains(v));
        connect(a, &QAction::toggled, this, [this, v](bool on)
        {
            if (on)
                m_compareViews.insert(v);
            else
                m_compareViews.remove(v);
            scheduleUpdate();
        });
        ++shown;
    }
    if (shown == 0)
        menu->addAction("No other map is open")->setEnabled(false);
}

static QString
startDirectory()
{
    const QString dir = QSettings().value("general/lastDirectory").toString();
    return (!dir.isEmpty() && QDir(dir).exists()) ? dir : QDir::homePath();
}

void
StatisticsDock::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Histogram", startDirectory() + "/histogram.png",
        "PNG Files (*.png);;All Files (*)");
    if (path.isEmpty())
        return;
    QSettings().setValue("general/lastDirectory",
                         QFileInfo(path).absolutePath());
    const QImage image = m_histView->grabWindow();
    if (image.isNull() || !image.save(path))
        QMessageBox::critical(this, "Export Histogram",
                              QString("Failed to save %1").arg(path));
}

// The bins with the counts and the density of every curve
void
StatisticsDock::exportCsv(const QString &givenPath)
{
    if (m_result.curves.empty())
    {
        QMessageBox::information(this, "Export Histogram",
                                 "There is no histogram to export.");
        return;
    }
    QString path = givenPath;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(this, "Export Histogram",
                                            startDirectory() + "/histogram.csv",
                                            "CSV Files (*.csv);;All Files (*)");
        if (path.isEmpty())
            return;
        QSettings().setValue("general/lastDirectory",
                             QFileInfo(path).absolutePath());
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "Export Histogram",
                              QString("Cannot write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    const double bw = (m_result.hi - m_result.lo) / m_result.bins;
    out << "# " << m_result.bins << " bins from " << m_result.lo << " to "
        << m_result.hi << "\n";
    for (const HistogramCurve &c : m_result.curves)
        out << "# " << c.name << ": " << c.valid << " pixels, mean " << c.mean
            << ", std " << c.std << "\n";

    auto clean = [](QString s)
    {
        s.replace(',', ';');
        return s;
    };
    out << "bin_lo,bin_hi";
    for (const HistogramCurve &c : m_result.curves)
        out << ",count " << clean(c.name);
    for (const HistogramCurve &c : m_result.curves)
        out << ",density " << clean(c.name);
    out << "\n";

    out.setRealNumberPrecision(10);
    for (int i = 0; i < m_result.bins; ++i)
    {
        out << (m_result.lo + i * bw) << ',' << (m_result.lo + (i + 1) * bw);
        for (const HistogramCurve &c : m_result.curves)
            out << ',' << c.counts[i];
        for (const HistogramCurve &c : m_result.curves)
            out << ',' << c.counts[i] / (std::max(1L, c.valid) * bw);
        out << "\n";
    }
}
