#include "aboutbanner.hpp"

#include <QConicalGradient>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QRadialGradient>
#include <QTimer>

namespace
{
constexpr int kBannerHeight = 140;
constexpr int kFrameMs      = 33;   // about 30 frames per second
constexpr qreal kTurnSecs   = 14.0; // time for the rainbow to turn once

// The family of the logo font (Noto Serif Black, from the resources). It is
// loaded once; empty when it cannot be loaded, which leaves the default font.
QString
logoFamily()
{
    static const QString family = []()
    {
        const int id = QFontDatabase::addApplicationFont(
            ":/resources/fonts/NotoSerif-Black.ttf");
        return QFontDatabase::applicationFontFamilies(id).value(0, QString());
    }();
    return family;
}
} // namespace

AboutBanner::AboutBanner(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(kBannerHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_timer = new QTimer(this);
    m_timer->setInterval(kFrameMs);
    connect(m_timer, &QTimer::timeout, this, [this]()
    {
        m_angle += 360.0 * (kFrameMs / 1000.0) / kTurnSecs;
        if (m_angle >= 360.0)
            m_angle -= 360.0;
        update();
    });
}

QSize
AboutBanner::sizeHint() const
{
    return {420, kBannerHeight};
}

QSize
AboutBanner::minimumSizeHint() const
{
    return {200, kBannerHeight};
}

void
AboutBanner::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_timer->start();
}

void
AboutBanner::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_timer->stop();
}

void
AboutBanner::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QRectF r = rect();

    // Base: a rainbow that is not linear but conical, the hues going around a
    // point near the banner's centre; it turns slowly. The last stop is the
    // first one, so there is no seam.
    QConicalGradient rainbow(
        r.center() + QPointF(r.width() * 0.15, r.height() * 0.15), m_angle);
    constexpr int kStops = 7;
    for (int i = 0; i <= kStops; ++i)
    {
        const qreal pos = qreal(i) / kStops;
        rainbow.setColorAt(pos,
                           QColor::fromHsvF(pos < 1.0 ? pos : 0.0, 0.7, 0.85));
    }
    p.fillRect(r, rainbow);

    // Shade the bottom left, round the name, so the white text stays readable
    QRadialGradient shade(r.bottomLeft(), r.width() * 0.7);
    shade.setColorAt(0.0, QColor(0, 0, 0, 190));
    shade.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(r, shade);

    // The name, at the bottom left
    QFont f = font();
    if (!logoFamily().isEmpty())
        f.setFamily(logoFamily());
    f.setPointSize(30);
    f.setWeight(QFont::Black); // 900
    p.setFont(f);

    // The tagline, in small spaced capitals at the bottom left
    QFont tf = font();
    tf.setPointSize(9);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    const QFontMetrics tfm(tf);
    const int x               = 18;
    const int taglineBaseline = int(r.bottom()) - 14 - tfm.descent();

    // "Hp" in white, "Vis" in orange, drawn one after the other, above it
    const QFontMetrics fm(f);
    const QString hp   = QStringLiteral("Hp");
    const QString vis  = QStringLiteral("Vis");
    const int baseline = taglineBaseline - tfm.ascent() - 4 - fm.descent();
    p.setPen(Qt::white);
    p.drawText(x, baseline, hp);
    p.setPen(QColor(255, 140, 0));
    p.drawText(x + fm.horizontalAdvance(hp), baseline, vis);

    // "HEALPIX" in bold, the rest regular, drawn one after the other
    QFont tfBold = tf;
    tfBold.setBold(true);
    const QString before = QStringLiteral("A MODERN ");
    const QString bold   = QStringLiteral("HEALPIX");
    const QString after  = QStringLiteral(" VIEWER");
    int tx               = x;
    p.setPen(QColor(246, 236, 224));
    p.setFont(tf);
    p.drawText(tx, taglineBaseline, before);
    tx += tfm.horizontalAdvance(before);
    p.setFont(tfBold);
    p.drawText(tx, taglineBaseline, bold);
    tx += QFontMetrics(tfBold).horizontalAdvance(bold);
    p.setFont(tf);
    p.drawText(tx, taglineBaseline, after);
}
