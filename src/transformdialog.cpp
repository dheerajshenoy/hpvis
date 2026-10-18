#include "transformdialog.hpp"

#include "datatransform.hpp"
#include "openglwidget.hpp"
#include "view.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent>

using datatransform::Kind;

namespace
{
QDoubleSpinBox *
makeSpin(double lo, double hi, double value, const QString &tip)
{
    auto *s = new QDoubleSpinBox();
    s->setDecimals(6);
    s->setRange(lo, hi);
    s->setValue(value);
    s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    s->setToolTip(tip);
    return s;
}
} // namespace

TransformDialog::TransformDialog(std::function<View *()> currentView,
                                 QWidget *parent)
    : QDialog(parent), m_currentView(std::move(currentView))
{
    setWindowTitle("Data Transform");
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);
    m_source     = new QLabel();
    m_source->setWordWrap(true);
    layout->addWidget(m_source);

    auto *form = new QFormLayout();
    m_kind     = new QComboBox();
    for (int i = 0; i < int(Kind::Count_); ++i)
        m_kind->addItem(datatransform::label(Kind(i)), i);
    m_kind->setToolTip("The transform changes the values of the map, not "
                       "just the colors. Unlike the color scale, it shows up "
                       "in the statistics, the histogram and the exports.");
    form->addRow("Transform:", m_kind);

    m_formula = new QLabel();
    m_formula->setWordWrap(true);
    form->addRow("Formula:", m_formula);

    m_mu = makeSpin(-1e30, 1e30, 0.0,
                    "Centre mu: the value that maps to zero before the "
                    "transform acts");
    m_sigma = makeSpin(1e-30, 1e30, 1.0,
                       "Scale sigma: how far from mu the transform starts to "
                       "bend (in the units of the map)");
    m_power = makeSpin(-100.0, 100.0, 2.0, "Exponent p of the power law");
    m_fromMap = new QPushButton("Use map mean and std");
    m_fromMap->setToolTip("Set mu to the mean and sigma to the standard "
                          "deviation of the valid pixels");
    form->addRow("mu:", m_mu);
    form->addRow("sigma:", m_sigma);
    form->addRow("Exponent p:", m_power);
    form->addRow(QString(), m_fromMap);

    m_title = new QLineEdit();
    form->addRow("Title:", m_title);
    layout->addLayout(form);

    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);
    m_progress = new QProgressBar();
    m_progress->setRange(0, 0);
    m_progress->hide();
    layout->addWidget(m_progress);

    auto *buttons   = new QHBoxLayout();
    m_computeButton = new QPushButton("Apply");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    buttons->addStretch();
    buttons->addWidget(m_computeButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(m_computeButton, &QPushButton::clicked, this,
            &TransformDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_title, &QLineEdit::textEdited, this,
            [this](const QString &) { m_title->setProperty("auto", false); });
    connect(m_kind, &QComboBox::currentIndexChanged, this,
            [this](int) { update(); });
    connect(m_fromMap, &QPushButton::clicked, this, [this]()
    {
        m_mu->setValue(m_mapMean);
        m_sigma->setValue(m_mapStd > 0.0 ? m_mapStd : 1.0);
    });
    update();
}

void
TransformDialog::refresh()
{
    m_view = m_currentView ? m_currentView() : nullptr;
    const Healpix_Map<float> *map
        = m_view ? m_view->glWidget()->healpixMap() : nullptr;
    m_title->setProperty("auto", true);
    m_message->hide();

    if (!map)
    {
        m_source->setText("Open a map first.");
        m_title->clear();
        update();
        return;
    }
    datatransform::meanStd(*map, m_mapMean, m_mapStd);
    m_source->setText(QString("Map: %1 (mean %2, std %3)")
                          .arg(m_view->displayName())
                          .arg(m_mapMean, 0, 'g', 5)
                          .arg(m_mapStd, 0, 'g', 5));
    // A scale of 1 would do nothing useful on a map in uK
    m_mu->setValue(0.0);
    m_sigma->setValue(m_mapStd > 0.0 ? m_mapStd : 1.0);
    update();
}

void
TransformDialog::update()
{
    const Kind kind = Kind(m_kind->currentData().toInt());
    m_formula->setText(datatransform::formula(kind));
    m_mu->setEnabled(datatransform::usesMu(kind));
    m_sigma->setEnabled(datatransform::usesSigma(kind));
    m_fromMap->setEnabled(datatransform::usesMu(kind));
    m_power->setEnabled(datatransform::usesPower(kind));
    m_computeButton->setEnabled(!m_busy && m_view
                                && m_view->glWidget()->healpixMap());

    if (m_view && m_title->property("auto").toBool())
    {
        const QSignalBlocker block(m_title);
        m_title->setText(QString("%1 [%2]").arg(m_view->displayName(),
                                                datatransform::label(kind)));
    }
}

void
TransformDialog::compute()
{
    if (m_busy || !m_view || !m_view->glWidget()->healpixMap())
        return;
    const Kind kind = Kind(m_kind->currentData().toInt());
    datatransform::Params p;
    p.mu    = m_mu->value();
    p.sigma = m_sigma->value();
    p.power = m_power->value();

    // The worker has its own copy: the tab may change while it runs
    auto map = std::make_shared<Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();
    QString title        = m_title->text().trimmed();
    if (title.isEmpty())
        title = QString("%1 [%2]").arg(m_view->displayName(),
                                       datatransform::label(kind));

    m_busy = true;
    m_progress->show();
    m_computeButton->setEnabled(false);
    m_message->hide();

    auto *watcher = new QFutureWatcher<long>(this);
    connect(watcher, &QFutureWatcher<long>::finished, this,
            [this, watcher, map, title, frame]()
    {
        const long lost = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_progress->hide();
        update();
        if (lost > 0)
        {
            m_message->setText(
                QString("%1 pixels have no finite value after the transform "
                        "and are UNSEEN in the new map.")
                    .arg(lost));
            m_message->show();
        }
        emit mapReady(map, title, frame);
    });
    watcher->setFuture(QtConcurrent::run(
        [map, kind, p]() { return datatransform::apply(*map, kind, p); }));
}
