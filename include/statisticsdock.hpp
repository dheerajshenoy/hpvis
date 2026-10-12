#pragma once

#include "histogram.hpp"
#include "openmap.hpp"

#include <QColor>
#include <QDockWidget>
#include <QPointer>
#include <QSet>
#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QQuickView;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;
class View;

// Dockable map statistics and the histogram of the pixel values. Follows the
// current tab and its masks; the histogram can show a field (column) of the
// map and be compared with the histograms of the other open maps.
class StatisticsDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit StatisticsDock(QWidget *parent = nullptr);

    // Shows the statistics of `view` (clears them when null)
    void setView(View *view);
    // Drops what refers to a view that is going away
    void forget(const View *view);
    // Refreshes the numbers of the view's statistics region
    void updateRegion();

    // The open maps, to compare their histograms with the current one
    void setOpenMapsProvider(std::function<QList<OpenMap>()> provider)
    {
        m_openMaps = std::move(provider);
    }

    // Histogram colors (the same as the power spectrum plot's)
    void setPlotColors(const QColor &background, const QColor &foreground,
                       const QColor &line);

private:
    void initHistogramUI(QWidget *parent);
    void fillFields();
    void scheduleUpdate();
    void updateHistogram();
    void showResult(bool keepRange);
    void buildCompareMenu();
    void exportImage();
    void exportCsv(const QString &path = QString());
    QString titleOf(const View *view) const;
    bool histogramWanted() const;

    // Statistics of the whole map and of the selected region
    void updateTable();
    void copyTable();
    QTableWidget *m_table      = nullptr;
    QLabel *m_regionHint       = nullptr;
    QToolButton *m_regionClear = nullptr;

    // Histogram
    QPointer<View> m_view;
    std::function<QList<OpenMap>()> m_openMaps;
    QSet<const View *> m_compareViews;

    QQuickView *m_histView       = nullptr;
    QWidget *m_histContainer     = nullptr;
    QComboBox *m_fieldCombo      = nullptr;
    QSpinBox *m_binsSpin         = nullptr;
    QCheckBox *m_densityCheck    = nullptr;
    QCheckBox *m_logCheck        = nullptr;
    QCheckBox *m_maskCheck       = nullptr;
    QCheckBox *m_clipCheck       = nullptr;
    QToolButton *m_compareButton = nullptr;
    QComboBox *m_modeCombo       = nullptr;
    QToolButton *m_exportButton  = nullptr;
    QLabel *m_histStatus         = nullptr;
    QTimer *m_debounce           = nullptr;

    QColor m_plotBackground;
    QColor m_plotForeground;
    QColor m_plotLine;

    // The last computed histograms and the colors of their curves
    HistogramResult m_result;
    QList<QColor> m_curveColors;
    int m_generation = 0; // a result of an older request is ignored
    bool m_running   = false;
    bool m_rerun     = false;
    bool m_dirty     = false; // the dock was hidden when the map changed
};
