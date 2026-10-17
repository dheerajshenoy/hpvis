#include "aboutbanner.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QConicalGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QTimer>

namespace
{
constexpr int kBannerHeight = 110;
constexpr int kFrameMs      = 33;     // about 30 frames per second
constexpr qreal kTurnSecs   = 14.0;   // time for the rainbow to turn once

// The family of the logo font (Major Mono Display, from the resources). It is
// loaded once; empty when it cannot be loaded, which leaves the default font.
QString
logoFamily()
{
    static const QString family = []()
    {
        const int id = QFontDatabase::addApplicationFont(
            ":/resources/fonts/Major-Mono-Display.ttf");
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
    QConicalGradient rainbow(r.center() + QPointF(r.width() * 0.15, r.height() * 0.15),
                             m_angle);
    constexpr int kStops = 7;
    for (int i = 0; i <= kStops; ++i)
    {
        const qreal pos = qreal(i) / kStops;
        rainbow.setColorAt(pos, QColor::fromHsvF(pos < 1.0 ? pos : 0.0, 0.7,
                                                 0.85));
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
    f.setBold(false); // the font has a single weight
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(r.adjusted(18, 0, -18, -12), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("hpvis"));
}
