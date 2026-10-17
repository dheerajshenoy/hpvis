#include "multipolefilterdialog.hpp"

#include "multipolefilter.hpp"
#include "openglwidget.hpp"
#include "resolution.hpp"
#include "view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

// Plot of the window f(l) from l = 0 to the analysis l max
class FilterPlot : public QWidget
{
public:
    explicit FilterPlot(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    }

    void setWindow(std::vector<double> w)
    {
        m_window = std::move(w);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = palette();
        p.fillRect(rect(), pal.base());

        const QFontMetrics fm(font());
        const QRectF plot(fm.horizontalAdvance("0.5") + 12, 8,
                          width() - fm.horizontalAdvance("0.5") - 22,
                          height() - fm.height() * 2 - 14);
        p.setPen(pal.color(QPalette::Mid));
        p.drawRect(plot);

        p.setPen(pal.color(QPalette::Text));
        for (double y : {0.0, 0.5, 1.0})
        {
            const double py = plot.bottom() - y * plot.height();
            p.drawText(QRectF(0, py - fm.height() / 2.0, plot.left() - 4,
                              fm.height()),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(y, 'g', 2));
            p.setPen(QPen(pal.color(QPalette::Mid), 1, Qt::DotLine));
            p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
            p.setPen(pal.color(QPalette::Text));
        }
        p.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width(),
                          fm.height()),
                   Qt::AlignLeft, "0");
        p.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width(),
                          fm.height()),
                   Qt::AlignRight,
                   QString::number(int(m_window.size()) - 1));
        p.drawText(QRectF(plot.left(), plot.bottom() + 2 + fm.height(),
                          plot.width(), fm.height()),
                   Qt::AlignCenter, "multipole l   (window f(l))");

        if (m_window.size() < 2)
            return;
        const double n = double(m_window.size() - 1);
        QPainterPath path;
        for (size_t l = 0; l < m_window.size(); ++l)
        {
            const QPointF pt(plot.left() + plot.width() * double(l) / n,
                             plot.bottom() - plot.height() * m_window[l]);
            l == 0 ? path.moveTo(pt) : path.lineTo(pt);
        }
        p.setClipRect(plot.adjusted(-1, -2, 1, 2));
        p.setPen(QPen(pal.color(QPalette::Highlight), 2));
        p.drawPath(path);
    }

private:
    std::vector<double> m_window;
};

MultipoleFilterDialog::MultipoleFilterDialog(
    std::function<View *()> currentView, QWidget *parent)
    : QDialog(parent), m_currentView(std::move(currentView))
{
    setWindowTitle("Multipole Filter");
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);
    m_source     = new QLabel();
    layout->addWidget(m_source);

    auto *form = new QFormLayout();
    m_type     = new QComboBox();
    m_type->addItems({"Low-pass (keep l ≤ l max)",
                      "High-pass (keep l ≥ l min)",
                      "Band-pass (keep l min ≤ l ≤ l max)",
                      "Band-stop (remove l min ≤ l ≤ l max)"});
    form->addRow("Filter:", m_type);

    m_lmin = new QSpinBox();
    m_lmin->setRange(0, 100000);
    m_lmin->setValue(2);
    m_lmin->setToolTip("Lowest multipole of the band; 2 removes the monopole "
                       "and dipole in a high-pass");
    form->addRow("l min:", m_lmin);

    m_lmaxCut = new QSpinBox();
    m_lmaxCut->setRange(0, 100000);
    m_lmaxCut->setValue(100);
    m_lmaxCut->setToolTip("Highest multipole of the band");
    form->addRow("l max:", m_lmaxCut);

    m_taper = new QComboBox();
    m_taper->addItems({"Cosine", "Gaussian", "Sharp (rings)"});
    m_taper->setToolTip("Shape of the edges. A sharp cut makes ringing in the "
                        "map, like a sharp cut in Fourier space.");
    form->addRow("Edge:", m_taper);

    m_taperWidth = new QSpinBox();
    m_taperWidth->setRange(1, 100000);
    m_taperWidth->setValue(10);
    m_taperWidth->setToolTip("Width of the ramp in multipoles, centred on the "
                             "cut (for Gaussian, the width is 2 sigma)");
    form->addRow("Edge width:", m_taperWidth);
    layout->addLayout(form);

    m_plot = new FilterPlot();
    layout->addWidget(m_plot);

    auto *form2 = new QFormLayout();
    m_polCheck  = new QCheckBox("Also filter Q and U (opens three maps)");
    form2->addRow(m_polCheck);

    m_iterations = new QSpinBox();
    m_iterations->setRange(0, 10);
    m_iterations->setValue(3);
    m_iterations->setToolTip("Refinements of the map2alm quadrature (0 = "
                             "plain map2alm)");
    form2->addRow("Iterations:", m_iterations);

    m_lmax = new QSpinBox();
    m_lmax->setToolTip("Highest l of the analysis. 3 * nside - 1 is the "
                       "usual choice; lower is faster and uses less memory. "
                       "Power above it is dropped.");
    form2->addRow("Analysis l max:", m_lmax);

    m_keepUnseen = new QCheckBox("Keep UNSEEN pixels UNSEEN");
    m_keepUnseen->setChecked(true);
    form2->addRow(m_keepUnseen);

    m_title = new QLineEdit();
    form2->addRow("Title:", m_title);
    layout->addLayout(form2);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);

    m_progress = new QProgressBar();
    m_progress->setRange(0, 0); // busy
    m_progress->hide();
    layout->addWidget(m_progress);

    auto *buttons   = new QHBoxLayout();
    m_computeButton = new QPushButton("Compute");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    buttons->addStretch();
    buttons->addWidget(m_computeButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(m_computeButton, &QPushButton::clicked, this,
            &MultipoleFilterDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_title, &QLineEdit::textEdited, this,
            [this](const QString &) { m_title->setProperty("auto", false); });

    auto changed = [this](int) { updateControls(); };
    connect(m_type, &QComboBox::currentIndexChanged, this, changed);
    connect(m_taper, &QComboBox::currentIndexChanged, this, changed);
    connect(m_lmin, &QSpinBox::valueChanged, this, changed);
    connect(m_lmaxCut, &QSpinBox::valueChanged, this, changed);
    connect(m_taperWidth, &QSpinBox::valueChanged, this, changed);
    connect(m_lmax, &QSpinBox::valueChanged, this, changed);
    updateControls();
}

int
MultipoleFilterDialog::analysisLmax() const
{
    return m_nside > 0 ? m_lmax->value() : std::max(m_lmaxCut->value() * 2, 64);
}

static FilterOptions
optionsFrom(const QComboBox *type, const QComboBox *taper, int lmin, int lmax,
            int width)
{
    FilterOptions o;
    o.type       = FilterType(type->currentIndex());
    o.taper      = FilterTaper(taper->currentIndex() == 0   ? 1
                               : taper->currentIndex() == 1 ? 2
                                                            : 0);
    o.lmin       = lmin;
    o.lmax       = lmax;
    o.taperWidth = width;
    return o;
}

void
MultipoleFilterDialog::updateControls()
{
    const auto type = FilterType(m_type->currentIndex());
    m_lmin->setEnabled(type != FilterType::LowPass);
    m_lmaxCut->setEnabled(type != FilterType::HighPass);
    const bool sharp = m_taper->currentIndex() == 2;
    m_taperWidth->setEnabled(!sharp);

    const FilterOptions o
        = optionsFrom(m_type, m_taper, m_lmin->value(), m_lmaxCut->value(),
                      m_taperWidth->value());
    m_plot->setWindow(filterWindow(o, analysisLmax()));
    updateTitle();
}

void
MultipoleFilterDialog::refresh()
{
    m_view = m_currentView ? m_currentView() : nullptr;
    const Healpix_Map<float> *map
        = m_view ? m_view->glWidget()->healpixMap() : nullptr;

    m_title->setProperty("auto", true); // a new map gets a new default title
    m_nside = map ? map->Nside() : 0;
    if (!map)
    {
        m_source->setText("Open a map first.");
        m_computeButton->setEnabled(false);
        m_title->clear();
        m_hint->clear();
        updateControls();
        return;
    }

    m_source->setText(QString("Map: %1, nside %2 (%3 pixels)")
                          .arg(m_view->displayName())
                          .arg(m_nside)
                          .arg(map->Npix()));
    m_computeButton->setEnabled(!m_busy);

    const bool pol = hasPolarization(m_view->filePath(), m_view->hdu())
                     && !m_view->isComputed();
    m_polCheck->setEnabled(pol);
    m_polCheck->setChecked(pol);
    m_polCheck->setToolTip(pol ? "Reads the T, Q and U columns of the map's "
                                 "HDU and filters them together as a spin-2 "
                                 "field (T, E and B get the same window)"
                               : "The map's HDU has no Q and U columns");

    {
        QSignalBlocker block(m_lmax);
        m_lmax->setRange(1, int(3 * m_nside - 1));
        m_lmax->setValue(int(3 * m_nside - 1));
    }
    updateControls();
}

void
MultipoleFilterDialog::updateTitle()
{
    if (m_nside < 1)
        return;

    if (m_title->text().isEmpty() || m_title->property("auto").toBool())
    {
        const QString what[] = {QString("l ≤ %1").arg(m_lmaxCut->value()),
                                QString("l ≥ %1").arg(m_lmin->value()),
                                QString("%1 ≤ l ≤ %2")
                                    .arg(m_lmin->value())
                                    .arg(m_lmaxCut->value()),
                                QString("without %1 ≤ l ≤ %2")
                                    .arg(m_lmin->value())
                                    .arg(m_lmaxCut->value())};
        m_title->setText(QString("%1 (%2)")
                             .arg(m_view ? m_view->displayName() : "map")
                             .arg(what[m_type->currentIndex()]));
        m_title->setProperty("auto", true);
    }

    QString hint;
    const int cut = m_type->currentIndex() == 1 ? m_lmin->value()
                                                : m_lmaxCut->value();
    if (cut > m_lmax->value())
        hint = "The cut is above the analysis l max: the filter has no "
               "effect there.";
    const double gb = harmonicMemoryEstimate(int(m_nside), m_lmax->value())
                      / (1024.0 * 1024.0 * 1024.0);
    hint += QString(" Needs roughly %1 GB of memory; this can take a long "
                    "time above nside 1024.")
                .arg(gb, 0, 'f', gb < 10 ? 1 : 0);
    m_hint->setText(hint.trimmed());
}

void
MultipoleFilterDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    m_computeButton->setEnabled(!busy && m_nside > 0);
}

void
MultipoleFilterDialog::showMessage(const QString &text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? "color: #d33;" : "");
    m_message->setVisible(!text.isEmpty());
}

void
MultipoleFilterDialog::compute()
{
    if (m_busy || !m_view || !m_view->glWidget()->healpixMap())
        return;

    // The worker gets its own copy: the tab may change while it runs
    auto in = std::make_shared<const Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();

    FilterOptions o
        = optionsFrom(m_type, m_taper, m_lmin->value(), m_lmaxCut->value(),
                      m_taperWidth->value());
    o.iterations   = m_iterations->value();
    o.analysisLmax = m_lmax->value();
    o.keepUnseen   = m_keepUnseen->isChecked();
    if (m_polCheck->isEnabled() && m_polCheck->isChecked())
    {
        o.polarization = true;
        o.path         = m_view->filePath();
        o.hdu          = m_view->hdu();
    }
    if (o.type == FilterType::BandPass || o.type == FilterType::BandStop)
        if (o.lmin > o.lmax)
        {
            showMessage("l min is above l max.", true);
            return;
        }

    const QString title = m_title->text().trimmed().isEmpty()
                              ? QString("Filtered")
                              : m_title->text().trimmed();

    setBusy(true);
    showMessage("Transforming to a_lm and back...", false);

    auto *watcher = new QFutureWatcher<FilterResult>(this);
    connect(watcher, &QFutureWatcher<FilterResult>::finished, this,
            [this, watcher, title, frame]()
    {
        FilterResult r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!r.map)
        {
            showMessage("Failed: " + r.error, true);
            return;
        }
        showMessage("Done. " + r.notes.join(" "), false);
        if (r.mapQ && r.mapU)
        {
            emit mapReady(std::move(r.map), title + " [T]", frame);
            emit mapReady(std::move(r.mapQ), title + " [Q]", frame);
            emit mapReady(std::move(r.mapU), title + " [U]", frame);
        }
        else
            emit mapReady(std::move(r.map), title, frame);
    });
    watcher->setFuture(
        QtConcurrent::run([in, o]() { return filterMap(*in, o); }));
}
