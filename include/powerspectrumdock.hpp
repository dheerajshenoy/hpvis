#pragma once

#include "healpix_map.h"
#include "openmap.hpp"

#include <QColor>
#include <QDockWidget>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QQuickView;
class View;
class QCheckBox;
class QProgressBar;
class QToolButton;
class QTimer;
struct PowerSpectrumJob;

struct Spectra
{
    std::vector<double> TT, EE, BB, TE, TB, EB;
};

// What the computation needs to know about a map
struct PowerSpectrumInput
{
    QString path; // FITS file the map came from (looked at for Q / U columns)
    int hdu = 0;  // 1-based HDU of the map; 0 = unknown
    std::shared_ptr<Healpix_Map<float>> map; // fallback / intensity-only map
    bool maskActive = false;                 // a mask is applied in the view
};

// Dockable power spectrum plot. It follows the current tab: each map keeps its
// own result, and nothing is recomputed until "Compute" is pressed.
class PowerSpectrumDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit PowerSpectrumDock(QWidget *parent = nullptr);

    // The map the dock shows (null when there is none)
    void setView(View *view);
    // The open maps, to compare their spectra with the current one
    void setOpenMapsProvider(std::function<QList<OpenMap>()> provider)
    {
        m_openMaps = std::move(provider);
    }

    // Plot colors (also set from the settings dialog)
    void setPlotColors(const QColor &background, const QColor &foreground,
                       const QColor &line);

    // Drops what was stored for a view that is going away
    void forget(const View *view);

signals:
    // The user picked a plot color in the dock
    void plotColorsChanged(const QColor &background, const QColor &foreground,
                           const QColor &line);

private:
    struct Result
    {
        Spectra spectra;
        bool unseen     = false; // some pixels were UNSEEN / NaN (zeroed)
        bool maskActive = false;
        int hdu         = 0;
        int column      = 0;
        int iterations  = 0; // refinement rounds used
        int lmaxMode    = 1; // the choice of the lmax combo (not the lmax)
        int lmax        = 0;
        int nside       = 0;
    };

    void initUI();
    void compute();
    void cancel();
    void updateProgress();
    void refresh();
    void updatePlot(const Spectra &spectra, bool keepRange = false);
    void applyPlotColors();
    void replot(bool keepRange = false);
    void buildCompareMenu();
    // Without a path the user is asked for the file; `unit` (0 = C_l, 1 = D_l,
    // -1 = ask) answers the question of what the values are
    void loadExternalCurve(const QString &path = QString(), int unit = -1);
    QString titleOf(const View *view) const;
    void pickPlotColor(QPushButton *button, QColor &color,
                       const QString &title);
    void showColorOnButton(QPushButton *button, const QColor &color);
    void exportCsv(const QString &path = QString());
    void exportImage();

private:
    QPointer<View> m_view;
    QHash<const View *, Result> m_results;

    // Comparison: other open maps (by view) and curves loaded from files
    struct External
    {
        QString name;
        Spectra spectra; // C_l, NaN where the file has no value
        bool enabled = true;
    };
    std::function<QList<OpenMap>()> m_openMaps;
    QSet<const View *> m_compareViews;
    QList<External> m_external;
    QComboBox *m_modeCombo   = nullptr;
    QCheckBox *m_commonCheck = nullptr; // limit all curves to a shared l range
    QToolButton *m_compareButton = nullptr;
    QSet<const View *> m_busy;
    // The running computation of each busy view (progress, cancel flag)
    QHash<const View *, std::shared_ptr<PowerSpectrumJob>> m_jobs;
    QTimer *m_progressTimer = nullptr;

    QQuickView *m_chartView     = nullptr;
    QWidget *m_chartContainer   = nullptr;
    QLabel *m_statusLabel       = nullptr;
    QLabel *m_warningLabel      = nullptr;
    QComboBox *m_spectrumCombo  = nullptr;
    QComboBox *m_accuracyCombo  = nullptr;
    QComboBox *m_lmaxCombo      = nullptr;
    QLabel *m_infoLabel         = nullptr;
    QComboBox *m_quantityCombo  = nullptr; // what the y axis shows
    QCheckBox *m_logXCheck      = nullptr;
    QCheckBox *m_logYCheck      = nullptr;
    QToolButton *m_exportButton = nullptr;
    QPushButton *m_bgButton     = nullptr;
    QPushButton *m_fgButton     = nullptr;
    QPushButton *m_lineButton   = nullptr;
    QColor m_plotBackground;
    QColor m_plotForeground;
    QColor m_plotLine;
    QPushButton *m_computeButton = nullptr;
    QProgressBar *m_progressBar  = nullptr;
    QLabel *m_progressLabel      = nullptr;
    bool m_plotLoaded            = false;
};
