#include "maskbuilderdock.hpp"

#include "openglwidget.hpp"
#include "unseen.hpp"
#include "view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

using namespace maskbuilder;

namespace
{
// The label the builder's mask has in the tab's mask list
const QString kMaskLabel = "Mask builder";

QDoubleSpinBox *
spin(double lo, double hi, int decimals, const QString &suffix = QString())
{
    auto *s = new QDoubleSpinBox();
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}
} // namespace

MaskBuilderDock::MaskBuilderDock(QWidget *parent)
    : QDockWidget("Mask Builder", parent)
{
    setObjectName("MaskBuilderDock");
    auto *body   = new QWidget();
    auto *layout = new QVBoxLayout(body);

    m_source = new QLabel("Open a map first.");
    m_source->setWordWrap(true);
    layout->addWidget(m_source);

    // Rules
    m_list = new QListWidget();
    m_list->setToolTip("The rules, applied from the top. Tick one to use it.");
    m_list->setMinimumHeight(110);
    layout->addWidget(m_list);

    auto *buttons = new QHBoxLayout();
    auto *add     = new QToolButton();
    add->setText("Add rule");
    add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    menu->addAction("Threshold", this,
                    [this]() { addRule(RuleType::Threshold); });
    menu->addAction("Galactic cut", this,
                    [this]() { addRule(RuleType::GalacticCut); });
    menu->addAction("Disc", this, [this]() { addRule(RuleType::Disc); });
    add->setMenu(menu);
    m_remove = new QPushButton("Remove");
    m_up     = new QPushButton("Up");
    m_down   = new QPushButton("Down");
    buttons->addWidget(add);
    buttons->addWidget(m_remove);
    buttons->addStretch();
    buttons->addWidget(m_up);
    buttons->addWidget(m_down);
    layout->addLayout(buttons);

    // Editor of the selected rule
    m_form    = new QFormLayout();
    m_combine = new QComboBox();
    m_combine->addItem("AND (keep only what both keep)",
                       int(Combine::And));
    m_combine->addItem("OR (keep what either keeps)", int(Combine::Or));
    m_combine->addItem("Subtract (drop what this rule keeps)",
                       int(Combine::Subtract));
    m_combine->setToolTip("How this rule joins the result of the rules above "
                          "it. The first rule starts the mask.");
    m_lo = spin(-1e30, 1e30, 6);
    m_lo->setToolTip("Keep pixels with a value of at least this");
    m_hi = spin(-1e30, 1e30, 6);
    m_hi->setToolTip("Keep pixels with a value of at most this");
    m_latitude = spin(0.0, 90.0, 2, "°");
    m_latitude->setToolTip("Cut out the band |b| below this galactic latitude");
    m_lon = spin(-360.0, 360.0, 3, "°");
    m_lat = spin(-90.0, 90.0, 3, "°");
    m_radius = spin(0.0, 180.0, 3, "°");
    m_discMode = new QComboBox();
    m_discMode->addItems({"Cut out the disc", "Keep only the disc"});
    m_form->addRow("Join:", m_combine);
    m_form->addRow("Minimum:", m_lo);
    m_form->addRow("Maximum:", m_hi);
    m_form->addRow("|b| at least:", m_latitude);
    m_form->addRow("Longitude:", m_lon);
    m_form->addRow("Latitude:", m_lat);
    m_form->addRow("Radius:", m_radius);
    m_form->addRow("Disc:", m_discMode);
    layout->addLayout(m_form);

    // Whole mask
    auto *whole = new QFormLayout();
    m_apod      = spin(0.0, 90.0, 2, "°");
    m_apod->setToolTip("Width (FWHM) of a Gaussian taper at the edges of the "
                       "mask: 0 keeps hard edges. A tapered mask makes much "
                       "less leakage in a power spectrum.");
    whole->addRow("Edge taper:", m_apod);
    m_fsky = new QLabel("-");
    m_fsky->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fsky->setToolTip("f_sky = sum(w) / Npix; w2 and w4 are the same with "
                       "w^2 and w^4 (the numbers used to correct a power "
                       "spectrum for the mask)");
    whole->addRow("Sky fraction:", m_fsky);
    layout->addLayout(whole);

    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);

    m_live = new QCheckBox("Update the map as I edit");
    m_live->setToolTip("Replace the mask of this tab after every change. "
                       "Off: use Apply.");
    layout->addWidget(m_live);

    auto *row = new QHBoxLayout();
    m_apply   = new QPushButton("Apply to map");
    m_apply->setToolTip("Put the mask in this tab's mask list (it replaces "
                        "the previous one from this builder)");
    m_unapply = new QPushButton("Remove");
    m_unapply->setToolTip("Take the builder's mask out of this tab");
    m_openMap = new QPushButton("Open as map");
    m_openMap->setToolTip("Open the mask as a new tab, to save it as FITS");
    row->addWidget(m_apply);
    row->addWidget(m_unapply);
    row->addWidget(m_openMap);
    layout->addLayout(row);

    // Painting by hand
    auto *paintBox = new QGroupBox("Paint");
    auto *pl       = new QVBoxLayout(paintBox);
    m_paintMode = new QCheckBox("Paint on the map");
    m_paintMode->setToolTip("Left drag paints with the tool below, right drag "
                            "does the opposite. Esc leaves paint mode, Ctrl+Z "
                            "undoes a stroke. The strokes make a mask called "
                            "\"Painted mask\" in this tab's mask list.");
    pl->addWidget(m_paintMode);
    auto *pf = new QFormLayout();
    m_paintTool = new QComboBox();
    m_paintTool->addItems({"Mask (hide pixels)", "Unmask (keep pixels)"});
    m_brush = spin(0.01, 90.0, 2, "°");
    m_brush->setValue(3.0);
    m_brush->setToolTip("Brush radius on the sky");
    m_hardness = new QSlider(Qt::Horizontal);
    m_hardness->setRange(0, 100);
    m_hardness->setValue(50);
    m_hardness->setToolTip("100: hard edge. Lower fades the brush towards its "
                           "rim, which gives a smooth mask edge.");
    pf->addRow("Tool:", m_paintTool);
    pf->addRow("Brush radius:", m_brush);
    pf->addRow("Hardness:", m_hardness);
    pl->addLayout(pf);
    auto *pr1 = new QHBoxLayout();
    m_maskAll = new QPushButton("Mask all");
    m_keepAll = new QPushButton("Keep all");
    m_invert  = new QPushButton("Invert");
    pr1->addWidget(m_maskAll);
    pr1->addWidget(m_keepAll);
    pr1->addWidget(m_invert);
    pl->addLayout(pr1);
    auto *pr2 = new QHBoxLayout();
    m_undo      = new QPushButton("Undo");
    m_redo      = new QPushButton("Redo");
    m_paintOpen = new QPushButton("Open as map");
    m_paintOpen->setToolTip("Open the painted mask as a new tab, to save it "
                            "as FITS");
    pr2->addWidget(m_undo);
    pr2->addWidget(m_redo);
    pr2->addWidget(m_paintOpen);
    pl->addLayout(pr2);
    m_paintFsky = new QLabel("-");
    pl->addWidget(m_paintFsky);
    layout->addWidget(paintBox);
    layout->addStretch();
    setWidget(body);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(300);
    connect(m_timer, &QTimer::timeout, this, &MaskBuilderDock::compute);

    connect(m_remove, &QPushButton::clicked, this, &MaskBuilderDock::removeRule);
    connect(m_up, &QPushButton::clicked, this, [this]() { moveRule(-1); });
    connect(m_down, &QPushButton::clicked, this, [this]() { moveRule(1); });
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { fillEditor(); });
    connect(m_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item)
    {
        State *s = state();
        const int row = m_list->row(item);
        if (m_filling || !s || row < 0 || row >= int(s->rules.size()))
            return;
        s->rules[size_t(row)].enabled = item->checkState() == Qt::Checked;
        scheduleCompute();
    });
    for (QDoubleSpinBox *b :
         {m_lo, m_hi, m_latitude, m_lon, m_lat, m_radius})
        connect(b, &QDoubleSpinBox::valueChanged, this,
                [this](double) { editorChanged(); });
    for (QComboBox *b : {m_combine, m_discMode})
        connect(b, &QComboBox::currentIndexChanged, this,
                [this](int) { editorChanged(); });
    connect(m_apod, &QDoubleSpinBox::valueChanged, this, [this](double v)
    {
        if (State *s = state())
        {
            s->apodFwhm = v;
            scheduleCompute();
        }
    });
    connect(m_apply, &QPushButton::clicked, this, &MaskBuilderDock::applyToMap);
    connect(m_unapply, &QPushButton::clicked, this,
            &MaskBuilderDock::removeFromMap);
    connect(m_openMap, &QPushButton::clicked, this, [this]()
    {
        if (m_last && m_view)
            emit mapReady(
                std::make_shared<Healpix_Map<float>>(*m_last),
                QString("%1 [mask]").arg(m_view->displayName()),
                m_view->glWidget()->mapFrame());
    });
    connect(m_paintMode, &QCheckBox::toggled, this, [this](bool on)
    {
        if (m_view)
        {
            m_view->glWidget()->setPaintMode(on);
            m_paintView = on ? m_view : nullptr;
        }
    });
    connect(m_paintTool, &QComboBox::currentIndexChanged, this,
            [this](int) { pushBrush(); });
    connect(m_brush, &QDoubleSpinBox::valueChanged, this,
            [this](double) { pushBrush(); });
    connect(m_hardness, &QSlider::valueChanged, this,
            [this](int) { pushBrush(); });
    auto onView = [this](auto fn)
    {
        return [this, fn]()
        {
            if (m_view)
                fn(m_view->glWidget());
        };
    };
    connect(m_maskAll, &QPushButton::clicked, this,
            onView([](OpenGLWidget *g) { g->paintFill(0); }));
    connect(m_keepAll, &QPushButton::clicked, this,
            onView([](OpenGLWidget *g) { g->paintFill(255); }));
    connect(m_invert, &QPushButton::clicked, this,
            onView([](OpenGLWidget *g) { g->paintInvert(); }));
    connect(m_undo, &QPushButton::clicked, this,
            onView([](OpenGLWidget *g) { g->paintUndo(); }));
    connect(m_redo, &QPushButton::clicked, this,
            onView([](OpenGLWidget *g) { g->paintRedo(); }));
    connect(m_paintOpen, &QPushButton::clicked, this, [this]()
    {
        if (!m_view)
            return;
        if (auto map = m_view->glWidget()->paintLayerMap())
            emit mapReady(map, QString("%1 [painted mask]").arg(m_view->displayName()),
                          m_view->glWidget()->mapFrame());
    });
    setView(nullptr);
}

void
MaskBuilderDock::pushBrush()
{
    if (!m_view)
        return;
    OpenGLWidget *gl = m_view->glWidget();
    gl->setPaintTool(m_paintTool->currentIndex() == 0
                         ? OpenGLWidget::PaintTool::Mask
                         : OpenGLWidget::PaintTool::Unmask);
    gl->setPaintBrush(m_brush->value(), m_hardness->value() / 100.0);
}

void
MaskBuilderDock::updatePaintUi()
{
    OpenGLWidget *gl = m_view ? m_view->glWidget() : nullptr;
    const bool ok    = gl && gl->healpixMap();
    m_paintMode->setEnabled(ok);
    {
        const QSignalBlocker b(m_paintMode);
        m_paintMode->setChecked(ok && gl->paintMode());
    }
    m_undo->setEnabled(ok && gl->canPaintUndo());
    m_redo->setEnabled(ok && gl->canPaintRedo());
    m_maskAll->setEnabled(ok);
    m_keepAll->setEnabled(ok);
    m_invert->setEnabled(ok);
    m_paintOpen->setEnabled(ok && gl->hasPaintLayer());
    m_paintFsky->setText(ok && gl->hasPaintLayer()
                             ? QString("Painted mask f_sky %1")
                                   .arg(gl->paintSkyFraction(), 0, 'f', 4)
                             : QString("No painted mask yet"));
}

// Follows the paint state of the tab that is shown
void
MaskBuilderDock::attachPaint(View *view)
{
    disconnect(m_paintConn);
    // Paint mode belongs to the tab it was switched on in
    if (m_paintView && m_paintView != view)
        m_paintView->glWidget()->setPaintMode(false);
    m_paintView = nullptr;
    if (view)
    {
        m_paintConn = connect(view->glWidget(), &OpenGLWidget::paintChanged,
                              this, [this]() { updatePaintUi(); });
        pushBrush();
        if (view->glWidget()->paintMode())
            m_paintView = view;
    }
    updatePaintUi();
}

MaskBuilderDock::State *
MaskBuilderDock::state()
{
    return m_view ? &m_states[m_view.data()] : nullptr;
}

void
MaskBuilderDock::setView(View *view)
{
    // The window calls this again for the same tab whenever its masks change
    // (including when this dock applies its own): that must not recompute,
    // or live updating would never stop
    if (view && view == m_view.data())
        return;
    m_view = view;
    attachPaint(view);
    m_last.reset();
    ++m_generation; // a result for the previous tab is no longer wanted
    const bool ok = view && view->glWidget()->healpixMap();
    setEnabled(ok);
    m_source->setText(ok ? QString("Map: %1").arg(view->displayName())
                         : QString("Open a map first."));
    {
        const QSignalBlocker b(m_apod);
        m_apod->setValue(ok ? state()->apodFwhm : 0.0);
    }
    fillList(0);
    m_fsky->setText("-");
    m_message->hide();
    if (ok && !state()->rules.empty())
        scheduleCompute();
}

void
MaskBuilderDock::forget(const View *view)
{
    m_states.remove(view);
    if (m_view.data() == view)
        m_view = nullptr;
}

void
MaskBuilderDock::fillList(int select)
{
    m_filling = true;
    m_list->clear();
    State *s = state();
    if (s)
    {
        const SkyFrame frame = m_view->glWidget()->mapFrame();
        for (const Rule &r : s->rules)
        {
            auto *item = new QListWidgetItem(r.summary(frame));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
            m_list->addItem(item);
        }
        if (!s->rules.empty())
            m_list->setCurrentRow(
                std::clamp(select, 0, int(s->rules.size()) - 1));
    }
    m_filling = false;
    fillEditor();
}

void
MaskBuilderDock::fillEditor()
{
    State *s      = state();
    const int row = m_list->currentRow();
    const bool ok = s && row >= 0 && row < int(s->rules.size());
    m_remove->setEnabled(ok);
    m_up->setEnabled(ok && row > 0);
    m_down->setEnabled(ok && row + 1 < int(s->rules.size()));
    updateEditorVisibility();
    if (!ok)
        return;

    const Rule &r = s->rules[size_t(row)];
    m_filling     = true;
    m_combine->setCurrentIndex(m_combine->findData(int(r.combine)));
    m_lo->setValue(r.lo);
    m_hi->setValue(r.hi);
    m_latitude->setValue(r.latitude);
    m_lon->setValue(r.lon);
    m_lat->setValue(r.lat);
    m_radius->setValue(r.radius);
    m_discMode->setCurrentIndex(r.keepInside ? 1 : 0);
    m_filling = false;
}

void
MaskBuilderDock::updateEditorVisibility()
{
    State *s      = state();
    const int row = m_list->currentRow();
    const bool ok = s && row >= 0 && row < int(s->rules.size());
    const RuleType t = ok ? s->rules[size_t(row)].type : RuleType::Threshold;
    auto show = [this](QWidget *w, bool on) { m_form->setRowVisible(w, on); };
    show(m_combine, ok && row > 0);
    show(m_lo, ok && t == RuleType::Threshold);
    show(m_hi, ok && t == RuleType::Threshold);
    show(m_latitude, ok && t == RuleType::GalacticCut);
    for (QWidget *w : {static_cast<QWidget *>(m_lon),
                       static_cast<QWidget *>(m_lat),
                       static_cast<QWidget *>(m_radius),
                       static_cast<QWidget *>(m_discMode)})
        show(w, ok && t == RuleType::Disc);
}

void
MaskBuilderDock::addRule(RuleType type)
{
    State *s = state();
    if (!s)
        return;
    Rule r;
    r.type = type;
    if (type == RuleType::Threshold)
    {
        // Start from the range of the map, so the rule keeps everything
        const Healpix_Map<float> *map = m_view->glWidget()->healpixMap();
        float lo = 1e30f, hi = -1e30f;
        for (int i = 0; i < map->Npix(); ++i)
            if (isSeen((*map)[i]))
            {
                lo = std::min(lo, (*map)[i]);
                hi = std::max(hi, (*map)[i]);
            }
        r.lo = lo;
        r.hi = hi;
    }
    s->rules.push_back(r);
    fillList(int(s->rules.size()) - 1);
    scheduleCompute();
}

void
MaskBuilderDock::removeRule()
{
    State *s      = state();
    const int row = m_list->currentRow();
    if (!s || row < 0 || row >= int(s->rules.size()))
        return;
    s->rules.erase(s->rules.begin() + row);
    fillList(row);
    scheduleCompute();
}

void
MaskBuilderDock::moveRule(int delta)
{
    State *s      = state();
    const int row = m_list->currentRow();
    const int to  = row + delta;
    if (!s || row < 0 || to < 0 || to >= int(s->rules.size()))
        return;
    std::swap(s->rules[size_t(row)], s->rules[size_t(to)]);
    fillList(to);
    scheduleCompute();
}

void
MaskBuilderDock::editorChanged()
{
    State *s      = state();
    const int row = m_list->currentRow();
    if (m_filling || !s || row < 0 || row >= int(s->rules.size()))
        return;
    Rule &r      = s->rules[size_t(row)];
    r.combine    = Combine(m_combine->currentData().toInt());
    r.lo         = m_lo->value();
    r.hi         = m_hi->value();
    r.latitude   = m_latitude->value();
    r.lon        = m_lon->value();
    r.lat        = m_lat->value();
    r.radius     = m_radius->value();
    r.keepInside = m_discMode->currentIndex() == 1;
    m_list->item(row)->setText(r.summary(m_view->glWidget()->mapFrame()));
    scheduleCompute();
}

void
MaskBuilderDock::scheduleCompute()
{
    m_timer->start();
}

void
MaskBuilderDock::compute()
{
    State *s = state();
    if (!s || !m_view || !m_view->glWidget()->healpixMap())
        return;
    if (s->rules.empty())
    {
        ++m_generation;
        m_last.reset();
        m_fsky->setText("-");
        if (m_live->isChecked() && s->applied)
            removeFromMap();
        return;
    }

    // The worker has its own copy: the tab may change while it runs
    auto map = std::make_shared<Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();
    const auto rules     = s->rules;
    const double apod    = s->apodFwhm;
    const quint64 gen    = ++m_generation;

    m_fsky->setText("computing...");
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this,
            [this, watcher, gen]()
    {
        Result r = watcher->result();
        watcher->deleteLater();
        if (gen != m_generation)
            return; // the rules or the tab changed since
        showResult(r);
        if (r.mask && m_live->isChecked())
            applyToMap();
    });
    watcher->setFuture(QtConcurrent::run([map, frame, rules, apod]()
    { return build(*map, frame, rules, apod); }));
}

void
MaskBuilderDock::showResult(const Result &r)
{
    if (!r.mask)
    {
        m_last.reset();
        m_fsky->setText("-");
        m_message->setText(r.error);
        m_message->setStyleSheet("color: #d33;");
        m_message->setVisible(!r.error.isEmpty());
        return;
    }
    m_last = r.mask;
    m_message->hide();
    m_fsky->setText(QString("f_sky %1   w2 %2   w4 %3")
                        .arg(r.fsky, 0, 'f', 4)
                        .arg(r.fsky2, 0, 'f', 4)
                        .arg(r.fsky4, 0, 'f', 4));
}

void
MaskBuilderDock::applyToMap()
{
    State *s = state();
    if (!s || !m_view || !m_last)
        return;
    OpenGLWidget *gl = m_view->glWidget();
    // Replace the builder's previous mask
    for (int i = gl->maskCount() - 1; i >= 0; --i)
        if (gl->maskPath(i) == "memory:" + kMaskLabel)
            gl->removeMask(i);
    MaskSource src;
    src.label = kMaskLabel;
    src.map   = m_last;
    gl->addMasks({src});
    s->applied = true;
}

void
MaskBuilderDock::removeFromMap()
{
    if (!m_view)
        return;
    OpenGLWidget *gl = m_view->glWidget();
    for (int i = gl->maskCount() - 1; i >= 0; --i)
        if (gl->maskPath(i) == "memory:" + kMaskLabel)
            gl->removeMask(i);
    if (State *s = state())
        s->applied = false;
}
