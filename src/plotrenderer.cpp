#include "plotrenderer.hpp"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPageSize>
#include <QPdfWriter>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>

namespace plotrenderer
{
namespace
{
// Round steps (1, 2, 5 x 10^n) covering [lo, hi], about `count` of them
QVector<double>
niceTicks(double lo, double hi, int count)
{
    QVector<double> out;
    if (!(hi > lo) || count < 1)
        return out;
    const double raw  = (hi - lo) / count;
    const double mag  = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double step
        = (norm < 1.5 ? 1.0 : norm < 3.5 ? 2.0 : norm < 7.5 ? 5.0 : 10.0)
          * mag;
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9;
         v += step)
        out << (std::abs(v) < step * 1e-9 ? 0.0 : v);
    return out;
}

// Light curve colors vanish on white paper: darken them
QColor
forPaper(const QColor &c)
{
    QColor h = c.toHsl();
    if (h.lightnessF() > 0.45)
        h.setHslF(h.hslHueF(), h.hslSaturationF(), 0.42);
    return h.toRgb();
}
} // namespace

void
paint(QPainter &p, const QRectF &rect, const Plot &plot)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    const QColor ink = plot.screenColors ? plot.foreground : QColor(20, 20, 20);
    const QColor gridColor
        = plot.screenColors
              ? QColor::fromRgbF(
                  plot.foreground.redF() * 0.16 + plot.background.redF() * 0.84,
                  plot.foreground.greenF() * 0.16
                      + plot.background.greenF() * 0.84,
                  plot.foreground.blueF() * 0.16
                      + plot.background.blueF() * 0.84)
              : QColor(225, 225, 225);
    auto curveColor = [&plot](const QColor &c)
    { return plot.screenColors ? c : forPaper(c); };

    QFont font = p.font();
    font.setPixelSize(std::max(8.0, rect.height() / 40.0));
    p.setFont(font);
    const QFontMetricsF fm(font);
    const double lh = fm.height();

    // The plot area, with room for the tick labels and the axis titles
    const QRectF area(rect.left() + 3.2 * lh, rect.top() + lh,
                      rect.width() - 4.4 * lh, rect.height() - 4.2 * lh);

    // Axis space: log10 for a log axis (its range is positive), else as is
    auto lin = [](double v, bool log)
    { return log ? std::log10(std::max(v, 1e-300)) : v; };
    const double xlo = lin(plot.xMin, plot.logX);
    const double xhi = lin(plot.xMax, plot.logX);
    const double ylo = lin(plot.yMin, plot.logY);
    const double yhi = lin(plot.yMax, plot.logY);
    if (area.width() <= 0 || area.height() <= 0 || !(xhi > xlo)
        || !(yhi > ylo))
    {
        p.restore();
        return;
    }
    auto px = [&](double v)
    {
        return area.left() + (lin(v, plot.logX) - xlo) / (xhi - xlo) * area.width();
    };
    auto py = [&](double v)
    {
        return area.bottom() - (lin(v, plot.logY) - ylo) / (yhi - ylo) * area.height();
    };

    // Ticks in axis space (decades for a log axis)
    auto ticksOf = [](double lo, double hi, bool log, int count)
    {
        if (!log)
            return niceTicks(lo, hi, count);
        QVector<double> t;
        for (int e = int(std::ceil(lo - 1e-9)); e <= int(std::floor(hi + 1e-9));
             ++e)
            t << e;
        return t;
    };
    const QVector<double> xt
        = ticksOf(xlo, xhi, plot.logX, std::max(2, int(area.width() / (7 * lh))));
    const QVector<double> yt
        = ticksOf(ylo, yhi, plot.logY, std::max(2, int(area.height() / (4 * lh))));

    // "10" with the exponent raised, or the number
    auto label = [&](double t, bool log) -> QString
    {
        return log ? QString::number(int(t)) : QString::number(t, 'g', 6);
    };
    // Draws a tick label of the log axis as 10^n
    auto drawLog = [&](double right, double midY, bool centerX, double cx,
                       double top, int e)
    {
        QFont small = font;
        small.setPixelSize(std::max(6.0, font.pixelSize() * 0.7));
        const QFontMetricsF sm(small);
        const QString base = "10", exp = QString::number(e);
        const double w = fm.horizontalAdvance(base) + sm.horizontalAdvance(exp);
        const double x0 = centerX ? cx - w / 2 : right - w;
        const double bl = centerX ? top + fm.ascent() : midY + fm.ascent() / 2;
        p.setFont(font);
        p.drawText(QPointF(x0, bl), base);
        p.setFont(small);
        p.drawText(QPointF(x0 + fm.horizontalAdvance(base), bl - fm.ascent() * 0.45),
                   exp);
        p.setFont(font);
    };

    // Grid
    p.setPen(QPen(gridColor, 0.0));
    for (double t : xt)
    {
        const double x = area.left() + (t - xlo) / (xhi - xlo) * area.width();
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (double t : yt)
    {
        const double y = area.bottom() - (t - ylo) / (yhi - ylo) * area.height();
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    // Curves, clipped to the plot; a gap where a value is not drawable
    p.save();
    p.setClipRect(area);
    for (const Curve &c : plot.curves)
    {
        QPainterPath path;
        bool open = false;
        for (int i = 0; i < c.ell.size() && i < c.values.size(); ++i)
        {
            const double v = c.values[i];
            if (!std::isfinite(v) || (plot.logY && v <= 0)
                || (plot.logX && c.ell[i] <= 0))
            {
                open = false;
                continue;
            }
            const QPointF pt(px(c.ell[i]), py(v));
            if (open)
                path.lineTo(pt);
            else
                path.moveTo(pt);
            open = true;
        }
        QPen pen(curveColor(c.color), std::max(1.0, lh / 10.0));
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.restore();

    // Frame, ticks, labels
    p.setPen(QPen(ink, std::max(0.8, lh / 14.0)));
    p.setBrush(Qt::NoBrush);
    p.drawRect(area);
    const double tick = lh * 0.3;
    for (double t : xt)
    {
        const double x = area.left() + (t - xlo) / (xhi - xlo) * area.width();
        p.setPen(ink);
        p.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + tick));
        if (plot.logX)
            drawLog(0, 0, true, x, area.bottom() + tick + lh * 0.1, int(t));
        else
            p.drawText(QPointF(x - fm.horizontalAdvance(label(t, false)) / 2,
                               area.bottom() + tick + fm.ascent() + lh * 0.1),
                       label(t, false));
    }
    for (double t : yt)
    {
        const double y = area.bottom() - (t - ylo) / (yhi - ylo) * area.height();
        p.setPen(ink);
        p.drawLine(QPointF(area.left() - tick, y), QPointF(area.left(), y));
        if (plot.logY)
            drawLog(area.left() - tick - lh * 0.2, y, false, 0, 0, int(t));
        else
            p.drawText(QPointF(area.left() - tick - lh * 0.2
                                   - fm.horizontalAdvance(label(t, false)),
                               y + fm.ascent() / 2),
                       label(t, false));
    }

    // Axis titles
    p.setPen(ink);
    p.drawText(QRectF(area.left(), rect.bottom() - 1.3 * lh, area.width(), lh),
               Qt::AlignCenter, plot.xTitle);
    p.save();
    p.translate(rect.left(), area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-area.height() / 2, 0, area.height(), lh),
               Qt::AlignCenter, plot.yTitle);
    p.restore();

    // Legend (top right, inside the plot) when there is more than one curve
    if (plot.curves.size() > 1)
    {
        double w = 0;
        for (const Curve &c : plot.curves)
            w = std::max(w, fm.horizontalAdvance(c.name));
        const double pad = lh * 0.4, sw = lh * 1.6;
        const QRectF box(area.right() - w - sw - 3 * pad - lh * 0.3,
                         area.top() + lh * 0.3, w + sw + 3 * pad,
                         plot.curves.size() * lh * 1.2 + 2 * pad - lh * 0.2);
        p.setPen(QPen(gridColor, 0.0));
        QColor boxFill = plot.screenColors ? plot.background : QColor(Qt::white);
        boxFill.setAlpha(220);
        p.setBrush(boxFill);
        p.drawRect(box);
        double y = box.top() + pad;
        for (const Curve &c : plot.curves)
        {
            const double mid = y + lh / 2;
            p.setPen(QPen(curveColor(c.color), std::max(1.0, lh / 8.0)));
            p.drawLine(QPointF(box.left() + pad, mid),
                       QPointF(box.left() + pad + sw, mid));
            p.setPen(ink);
            p.drawText(QPointF(box.left() + 2 * pad + sw, y + fm.ascent()),
                       c.name);
            y += lh * 1.2;
        }
    }
    p.restore();
}

bool
writeVector(const QString &path, const Plot &plot, const QSizeF &shown,
            QString *error)
{
    auto fail = [error](const QString &why)
    {
        if (error)
            *error = why;
        return false;
    };
    const QString ext = path.section('.', -1).toLower();
    if (ext != "svg" && ext != "pdf")
        return fail("Unknown format");

    // Same proportions as the plot on screen
    const double aspect
        = shown.width() > 0 ? shown.height() / shown.width() : 0.65;
    const double w = 640.0;
    const QRectF rect(0, 0, w, w * std::clamp(aspect, 0.4, 1.0));
    const QColor bg = plot.screenColors ? plot.background : QColor(Qt::white);

    if (ext == "svg")
    {
        QSvgGenerator svg;
        svg.setFileName(path);
        svg.setSize(rect.size().toSize());
        svg.setViewBox(rect);
        svg.setTitle("Plot");
        QPainter p(&svg);
        if (!p.isActive())
            return fail("Cannot write " + path);
        p.fillRect(rect, bg);
        paint(p, rect, plot);
        p.end();
    }
    else
    {
        QPdfWriter pdf(path);
        pdf.setTitle("Plot");
        pdf.setResolution(72); // 1 unit = 1 pt
        pdf.setPageSize(QPageSize(rect.size(), QPageSize::Point));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter p(&pdf);
        if (!p.isActive())
            return fail("Cannot write " + path);
        p.fillRect(rect, bg);
        paint(p, rect, plot);
        p.end();
    }
    return QFileInfo::exists(path) ? true : fail("Cannot write " + path);
}
} // namespace plotrenderer
