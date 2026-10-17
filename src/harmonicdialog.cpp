#include "harmonicdialog.hpp"

#include "harmonicmaps.hpp"
#include "openglwidget.hpp"
#include "smoothing.hpp"
#include "view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>

HarmonicDialog::HarmonicDialog(std::function<View *()> currentView,
                               QWidget *parent)
    : QDialog(parent), m_currentView(std::move(currentView))
{
    setWindowTitle("Derivatives and E/B Maps");
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    m_source     = new QLabel();
    layout->addWidget(m_source);

    auto *form  = new QFormLayout();
    m_operation = new QComboBox();
    m_operation->addItem("Laplacian (∇²T)");
    m_operation->addItem("Gradient magnitude (|∇T|)");
    m_operation->addItem("E and B maps (from Q and U)");
    m_operation->setToolTip(
        "Laplacian: multiplies each a_lm by -l(l+1). Gradient: the "
        "derivatives along theta and phi, combined. E / B: splits the "
        "polarization (Q, U) into its gradient-like and curl-like parts.");
    form->addRow("Operation:", m_operation);

    m_iterations = new QSpinBox();
    m_iterations->setRange(0, 10);
    m_iterations->setValue(3);
    m_iterations->setToolTip("Refinements of the map2alm quadrature (0 = "
                             "plain map2alm)");
    form->addRow("Iterations:", m_iterations);

    m_lmax = new QSpinBox();
    m_lmax->setToolTip("Highest l of the analysis. 3 * nside - 1 is the "
                       "usual choice; lower is faster and uses less memory.");
    form->addRow("Analysis l max:", m_lmax);

    m_keepUnseen = new QCheckBox("Keep UNSEEN pixels UNSEEN");
    m_keepUnseen->setChecked(true);
    form->addRow(m_keepUnseen);
    layout->addLayout(form);

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
            &HarmonicDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_operation, &QComboBox::currentIndexChanged, this,
            [this](int) { updateOperation(); });
}

void
HarmonicDialog::refresh()
{
    m_view = m_currentView ? m_currentView() : nullptr;
    const Healpix_Map<float> *map
        = m_view ? m_view->glWidget()->healpixMap() : nullptr;

    m_nside = map ? map->Nside() : 0;
    if (!map)
    {
        m_source->setText("Open a map first.");
        m_computeButton->setEnabled(false);
        return;
    }
    m_source->setText(QString("Map: %1, nside %2 (%3 pixels)")
                          .arg(m_view->displayName())
                          .arg(m_nside)
                          .arg(map->Npix()));

    // E / B needs the Q and U columns, which are read from the file
    m_hasPol = !m_view->isComputed()
               && hasPolarization(m_view->filePath(), m_view->hdu());

    QSignalBlocker block(m_lmax);
    m_lmax->setRange(1, int(3 * m_nside - 1));
    m_lmax->setValue(int(3 * m_nside - 1));
    updateOperation();
}

// E / B can only be chosen when the map has Q and U
void
HarmonicDialog::updateOperation()
{
    const bool eb = m_operation->currentIndex() == 2;
    if (eb && !m_hasPol)
        showMessage("The map's HDU has no T, Q and U columns.", true);
    else
        showMessage(QString(), false);
    m_computeButton->setEnabled(!m_busy && m_nside > 0 && !(eb && !m_hasPol));
}

void
HarmonicDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    m_operation->setEnabled(!busy);
    m_computeButton->setEnabled(!busy && m_nside > 0);
}

void
HarmonicDialog::showMessage(const QString &text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? "color: #d33;" : "");
    m_message->setVisible(!text.isEmpty());
}

void
HarmonicDialog::compute()
{
    if (m_busy || !m_view || !m_view->glWidget()->healpixMap())
        return;

    // The worker gets its own copy: the tab may change while it runs
    auto in = std::make_shared<const Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();

    HarmonicOptions o;
    o.op = static_cast<HarmonicOp>(m_operation->currentIndex());
    o.iterations = m_iterations->value();
    o.lmax       = m_lmax->value();
    o.keepUnseen = m_keepUnseen->isChecked();
    o.path       = m_view->filePath();
    o.hdu        = m_view->hdu();

    static const char *const suffix[] = {"∇²", "|∇|", "E"};
    const QString base = m_view->displayName();
    const QString title
        = o.op == HarmonicOp::EB
              ? base
              : QString("%1 of %2").arg(suffix[int(o.op)], base);

    setBusy(true);
    showMessage("Transforming to a_lm and back...", false);

    auto *watcher = new QFutureWatcher<HarmonicResult>(this);
    const HarmonicOp op = o.op;
    connect(watcher, &QFutureWatcher<HarmonicResult>::finished, this,
            [this, watcher, title, frame, op]()
    {
        HarmonicResult r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!r.map)
        {
            showMessage("Failed: " + r.error, true);
            return;
        }
        showMessage("Done. " + r.notes.join(" "), false);
        if (op == HarmonicOp::EB)
        {
            emit mapReady(std::move(r.map), title + " [E]", frame);
            emit mapReady(std::move(r.mapB), title + " [B]", frame);
        }
        else
            emit mapReady(std::move(r.map), title, frame);
    });
    watcher->setFuture(
        QtConcurrent::run([in, o]() { return harmonicMap(*in, o); }));
}
