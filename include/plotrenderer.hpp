#pragma once

#include <QColor>
#include <QList>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

// Draws the power spectrum plot with a QPainter, so that it can go to a
// vector device (SVG, PDF). The on-screen plot is a Qt Graphs scene, which
// can only be grabbed as pixels.
namespace plotrenderer
{
struct Curve
{
    QString name;
    QColor color;
    QVector<double> ell;
    QVector<double> values;
};

struct Plot
{
    QList<Curve> curves;
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1; // the visible range
    bool logX = false, logY = false;
    QString xTitle;
    QString yTitle;
    // false: dark ink and the curve colors darkened, for white paper.
    // true: the colors of the plot on screen.
    bool screenColors = false;
    QColor background = Qt::white;
    QColor foreground = QColor(20, 20, 20);
};

// Draws `plot` into `rect` (device units of the painter). The caller fills
// the background (plot.background with screenColors, else white).
void paint(QPainter &p, const QRectF &rect, const Plot &plot);

// Writes `plot` to an .svg or .pdf file (by the extension of `path`), with
// the proportions of the plot as shown. On failure returns false and sets
// `error`.
bool writeVector(const QString &path, const Plot &plot, const QSizeF &shown,
                 QString *error = nullptr);
} // namespace plotrenderer
