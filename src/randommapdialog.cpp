#include "randommapdialog.hpp"

#include "appsettings.hpp"
#include "randommap.hpp"
#include "units.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <limits>
#include <random>

RandomMapDialog::RandomMapDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Generate Random Map");
    setMinimumWidth(430);

    auto *layout = new QVBoxLayout(this);
    auto *form   = new QFormLayout();

    m_kind = new QComboBox();
    m_kind->addItem("Gaussian noise", int(RandomKind::GaussianNoise));
    m_kind->addItem("Uniform noise", int(RandomKind::UniformNoise));
    m_kind->addItem("CMB temperature (from the theory spectrum)",
                    int(RandomKind::CmbTemperature));
    m_kind->addItem("CMB temperature and polarization (T, Q, U)",
                    int(RandomKind::CmbPolarization));
    form->addRow("Map:", m_kind);

    m_nside = new QComboBox();
    for (int n = 1; n <= 8192; n *= 2)
        m_nside->addItem(QString("%1  (%2 pixels)").arg(n).arg(12L * n * n), n);
    m_nside->setCurrentIndex(8); // 256
    form->addRow("nside:", m_nside);

    m_a      = new QDoubleSpinBox();
    m_b      = new QDoubleSpinBox();
    m_aLabel = new QLabel();
    m_bLabel = new QLabel();
    for (QDoubleSpinBox *s : {m_a, m_b})
    {
        s->setDecimals(6);
        s->setRange(-1e12, 1e12);
    }
    form->addRow(m_aLabel, m_a);
    form->addRow(m_bLabel, m_b);

    m_unit = new QComboBox();
    m_unit->addItems({"uK", "mK", "K"});
    m_unit->setToolTip("The unit of the map. The theory spectrum is in uK^2 "
                       "and is scaled to it.");
    m_unitLabel = new QLabel("Unit:");
    form->addRow(m_unitLabel, m_unit);

    m_fwhm = new QDoubleSpinBox();
    m_fwhm->setDecimals(2);
    m_fwhm->setRange(0.0, 1e4);
    m_fwhm->setSuffix(" arcmin");
    m_fwhm->setToolTip("FWHM of a Gaussian beam to smooth the map with; 0 "
                       "leaves it unsmoothed");
    m_fwhmLabel = new QLabel("Beam FWHM:");
    form->addRow(m_fwhmLabel, m_fwhm);

    auto *seedRow = new QHBoxLayout();
    m_randomSeed  = new QCheckBox("Random");
    m_randomSeed->setChecked(true);
    m_seed = new QSpinBox();
    m_seed->setRange(0, std::numeric_limits<int>::max());
    m_seed->setValue(1);
    seedRow->addWidget(m_randomSeed);
    seedRow->addWidget(m_seed, 1);
    form->addRow("Seed:", seedRow);
    layout->addLayout(form);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);
    m_progress = new QProgressBar();
    m_progress->setRange(0, 0);
    m_progress->hide();
    layout->addWidget(m_progress);

    auto *buttons   = new QHBoxLayout();
    m_computeButton = new QPushButton("Generate");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    buttons->addStretch();
    buttons->addWidget(m_computeButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    const QSettings s;
    m_nside->setCurrentIndex(std::clamp(s.value("random/nsideIndex", 8).toInt(),
                                        0, m_nside->count() - 1));
    m_kind->setCurrentIndex(
        std::clamp(s.value("random/kind", 0).toInt(), 0, m_kind->count() - 1));
    m_unit->setCurrentIndex(AppSettings::load().theoryUnit);

    connect(m_kind, &QComboBox::currentIndexChanged, this,
            [this](int) { updateOptions(); });
    connect(m_randomSeed, &QCheckBox::toggled, m_seed,
            [this](bool on) { m_seed->setEnabled(!on); });
    m_seed->setEnabled(false);
    connect(m_computeButton, &QPushButton::clicked, this,
            &RandomMapDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    updateOptions();
}

void
RandomMapDialog::updateOptions()
{
    const auto kind = RandomKind(m_kind->currentData().toInt());
    const bool cmb  = kind == RandomKind::CmbTemperature
                     || kind == RandomKind::CmbPolarization;
    const bool noise = !cmb;

    m_a->setVisible(noise);
    m_b->setVisible(noise);
    m_aLabel->setVisible(noise);
    m_bLabel->setVisible(noise);
    m_unit->setVisible(cmb);
    m_unitLabel->setVisible(cmb);
    m_fwhm->setVisible(cmb);
    m_fwhmLabel->setVisible(cmb);

    if (kind == RandomKind::GaussianNoise)
    {
        m_aLabel->setText("Mean:");
        m_bLabel->setText("Sigma:");
        m_a->setValue(0.0);
        m_b->setValue(1.0);
        m_hint->setText("Every pixel is an independent Gaussian number.");
    }
    else if (kind == RandomKind::UniformNoise)
    {
        m_aLabel->setText("Minimum:");
        m_bLabel->setText("Maximum:");
        m_a->setValue(0.0);
        m_b->setValue(1.0);
        m_hint->setText("Every pixel is an independent number between the "
                        "minimum and the maximum.");
    }
    else
    {
        m_hint->setText(
            "A Gaussian random sky with the power spectrum chosen in "
            "Tools > Settings > Theory (the Planck 2018 best fit, or your "
            "file), drawn up to l = 3 nside - 1 or the end of the spectrum. "
            "The map is opened in the unit you choose.");
    }
}

void
RandomMapDialog::showMessage(const QString &text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? "color: #d33;" : "");
    m_message->setVisible(!text.isEmpty());
}

void
RandomMapDialog::compute()
{
    if (m_busy)
        return;

    RandomOptions o;
    o.kind  = RandomKind(m_kind->currentData().toInt());
    o.nside = m_nside->currentData().toInt();
    o.a     = m_a->value();
    o.b     = m_b->value();
    o.seed  = m_randomSeed->isChecked()
                  ? unsigned(std::random_device{}() & 0x7fffffff)
                  : unsigned(m_seed->value());
    o.fwhmArcmin = m_fwhm->value();

    const AppSettings settings = AppSettings::load();
    o.theory.preset            = TheoryPreset(settings.theoryPreset);
    o.theory.file              = settings.theoryFile;
    o.theory.unit              = TheoryUnit(m_unit->currentIndex());

    QSettings s;
    s.setValue("random/nsideIndex", m_nside->currentIndex());
    s.setValue("random/kind", m_kind->currentIndex());

    const bool cmb = o.kind == RandomKind::CmbTemperature
                     || o.kind == RandomKind::CmbPolarization;
    static const char *names[]
        = {"Gaussian noise", "Uniform noise", "CMB", "CMB"};
    const QString title = QString("Random %1 (nside %2, seed %3)")
                              .arg(names[int(o.kind)])
                              .arg(o.nside)
                              .arg(o.seed);
    static const units::Unit unitOf[]
        = {units::Unit::uK_CMB, units::Unit::mK_CMB, units::Unit::K_CMB};
    const QString unit = cmb ? units::label(unitOf[m_unit->currentIndex()])
                             : QString();

    m_busy = true;
    m_progress->show();
    m_computeButton->setEnabled(false);
    showMessage(QString(), false);

    auto *watcher = new QFutureWatcher<RandomResult>(this);
    connect(watcher, &QFutureWatcher<RandomResult>::finished, this,
            [this, watcher, title, unit, o]()
    {
        const RandomResult r = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_progress->hide();
        m_computeButton->setEnabled(true);
        if (!r.error.isEmpty() || !r.map)
        {
            showMessage("Cannot generate the map: " + r.error, true);
            return;
        }
        // CMB maps are in Galactic coordinates like the usual CMB maps
        const SkyFrame frame = SkyFrame::Galactic;
        if (r.mapQ && r.mapU)
        {
            emit mapReady(r.map, title + " [T]", frame, unit);
            emit mapReady(r.mapQ, title + " [Q]", frame, unit);
            emit mapReady(r.mapU, title + " [U]", frame, unit);
        }
        else
            emit mapReady(r.map, title, frame, unit);
        if (r.lmax > 0)
            showMessage(QString("Drawn up to l = %1.").arg(r.lmax), false);
    });
    watcher->setFuture(QtConcurrent::run([o]() { return generateRandomMap(o); }));
}
