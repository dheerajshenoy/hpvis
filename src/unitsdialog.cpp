#include "unitsdialog.hpp"

#include "openglwidget.hpp"
#include "parallel.hpp"
#include "units.hpp"
#include "unseen.hpp"
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
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

using units::Unit;

UnitsDialog::UnitsDialog(std::function<View *()> currentView, QWidget *parent)
    : QDialog(parent), m_currentView(std::move(currentView))
{
    setWindowTitle("Convert Units");
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    m_source     = new QLabel();
    m_source->setWordWrap(true);
    layout->addWidget(m_source);

    auto *form = new QFormLayout();
    m_from     = new QComboBox();
    m_to       = new QComboBox();
    for (int i = 0; i < int(Unit::Count_); ++i)
    {
        m_from->addItem(units::label(Unit(i)), i);
        m_to->addItem(units::label(Unit(i)), i);
    }
    m_from->setToolTip("The unit of the map now. Read from the FITS TUNIT "
                       "keyword when it is there (a plain K, mK or uK is "
                       "taken as a CMB temperature).");
    m_to->setToolTip("The unit to convert to");
    form->addRow("From:", m_from);
    form->addRow("To:", m_to);

    auto *freqRow = new QHBoxLayout();
    m_frequency   = new QDoubleSpinBox();
    m_frequency->setDecimals(3);
    m_frequency->setRange(0.001, 1e6);
    m_frequency->setValue(143.0);
    m_frequency->setToolTip(
        "The frequency of the map. Needed for Rayleigh-Jeans temperatures "
        "and for Jy/sr. The conversion is for that single frequency, not "
        "integrated over a detector bandpass.");
    m_frequencyUnit = new QComboBox();
    m_frequencyUnit->addItems({"MHz", "GHz", "THz"});
    m_frequencyUnit->setCurrentIndex(1);
    freqRow->addWidget(m_frequency, 1);
    freqRow->addWidget(m_frequencyUnit);
    form->addRow("Frequency:", freqRow);

    m_factor = new QLabel();
    form->addRow("Factor:", m_factor);
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
    m_computeButton = new QPushButton("Convert");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    buttons->addStretch();
    buttons->addWidget(m_computeButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    const QSettings settings;
    m_frequency->setValue(settings.value("units/frequency", 143.0).toDouble());
    m_frequencyUnit->setCurrentIndex(
        std::clamp(settings.value("units/frequencyUnit", 1).toInt(), 0, 2));

    connect(m_computeButton, &QPushButton::clicked, this, &UnitsDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_title, &QLineEdit::textEdited, this,
            [this](const QString &) { m_title->setProperty("auto", false); });
    for (QComboBox *box : {m_from, m_to, m_frequencyUnit})
        connect(box, &QComboBox::currentIndexChanged, this,
                [this](int) { update(); });
    connect(m_frequency, &QDoubleSpinBox::valueChanged, this,
            [this](double) { update(); });
}

double
UnitsDialog::frequencyGHz() const
{
    static const double toGHz[] = {1e-3, 1.0, 1e3};
    return m_frequency->value() * toGHz[m_frequencyUnit->currentIndex()];
}

void
UnitsDialog::refresh()
{
    m_view = m_currentView ? m_currentView() : nullptr;
    const Healpix_Map<float> *map
        = m_view ? m_view->glWidget()->healpixMap() : nullptr;
    m_title->setProperty("auto", true);
    m_message->hide();

    if (!map)
    {
        m_source->setText("Open a map first.");
        m_computeButton->setEnabled(false);
        m_title->clear();
        return;
    }

    const QString tunit = m_view->unit();
    const auto known    = units::fromFits(tunit);
    m_source->setText(
        QString("Map: %1. Unit in the file: %2")
            .arg(m_view->displayName(),
                 tunit.isEmpty() ? QString("not given")
                                 : QString("%1%2").arg(
                                       tunit, known ? QString()
                                                    : " (not recognized)")));
    {
        const QSignalBlocker b1(m_from), b2(m_to);
        const Unit from = known.value_or(Unit::K_CMB);
        m_from->setCurrentIndex(int(from));
        m_to->setCurrentIndex(
            int(from == Unit::K_CMB ? Unit::uK_CMB : Unit::K_CMB));
    }
    update();
}

void
UnitsDialog::update()
{
    const Unit from = Unit(m_from->currentData().toInt());
    const Unit to   = Unit(m_to->currentData().toInt());
    const bool freq = units::needsFrequency(from, to);
    m_frequency->setEnabled(freq);
    m_frequencyUnit->setEnabled(freq);

    const double f = units::factor(from, to, frequencyGHz());
    const bool ok  = std::isfinite(f) && f > 0.0;
    m_factor->setText(ok ? QString("1 %1 = %2 %3")
                               .arg(units::label(from))
                               .arg(f, 0, 'g', 8)
                               .arg(units::label(to))
                         : QString("-"));
    m_computeButton->setEnabled(ok && !m_busy && m_view
                                && m_view->glWidget()->healpixMap());

    if (m_view && m_title->property("auto").toBool())
    {
        const QSignalBlocker block(m_title);
        m_title->setText(
            QString("%1 [%2]").arg(m_view->displayName(), units::label(to)));
    }
}

void
UnitsDialog::compute()
{
    if (m_busy || !m_view || !m_view->glWidget()->healpixMap())
        return;
    const Unit from = Unit(m_from->currentData().toInt());
    const Unit to   = Unit(m_to->currentData().toInt());
    const double f  = units::factor(from, to, frequencyGHz());
    if (!std::isfinite(f) || f <= 0.0)
        return;

    QSettings settings;
    settings.setValue("units/frequency", m_frequency->value());
    settings.setValue("units/frequencyUnit", m_frequencyUnit->currentIndex());

    // The worker has its own copy: the tab may change while it runs
    auto map = std::make_shared<Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();
    QString title        = m_title->text().trimmed();
    if (title.isEmpty())
        title = QString("%1 [%2]").arg(m_view->displayName(), units::label(to));
    const QString unit = units::label(to);

    m_busy = true;
    m_progress->show();
    m_computeButton->setEnabled(false);
    m_message->hide();

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, map, title, frame, unit]()
    {
        watcher->deleteLater();
        m_busy = false;
        m_progress->hide();
        update();
        emit mapReady(map, title, frame, unit);
    });
    const float scale = float(f);
    watcher->setFuture(QtConcurrent::run([map, scale]()
    {
        float *data = &(*map)[0];
        parallel::forRange(long(map->Npix()), [=](long begin, long end)
        {
            for (long i = begin; i < end; ++i)
                if (isSeen(data[i]))
                    data[i] *= scale;
        });
    }));
}
