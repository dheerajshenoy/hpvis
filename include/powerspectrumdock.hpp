#pragma once

#include "healpix_map.h"

#include <QDockWidget>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
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
    QString path;   // FITS file the map came from (looked at for Q / U columns)
    int hdu = 0;    // 1-based HDU of the map; 0 = unknown
    std::shared_ptr<Healpix_Map<float>> map; // fallback / intensity-only map
    bool maskActive = false; // a mask is applied in the view
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
    // Drops what was stored for a view that is going away
    void forget(const View *view);

private:
    struct Result
    {
        Spectra spectra;
        bool unseen     = false; // some pixels were UNSEEN / NaN (zeroed)
        bool maskActive = false;
        int hdu         = 0;
        int column      = 0;
        int iterations  = 0; // refinement rounds used
        int lmax        = 0;
        int nside       = 0;
    };

    void initUI();
    void compute();
    void cancel();
    void updateProgress();
    void refresh();
    void updatePlot(const Spectra &spectra);
    void applyPlotScales();
    void exportCsv();
    void exportImage();

private:
    QPointer<View> m_view;
    QHash<const View *, Result> m_results;
    QSet<const View *> m_busy;
    // The running computation of each busy view (progress, cancel flag)
    QHash<const View *, std::shared_ptr<PowerSpectrumJob>> m_jobs;
    QTimer *m_progressTimer = nullptr;

    QQuickView *m_chartView    = nullptr;
    QWidget *m_chartContainer  = nullptr;
    QLabel *m_statusLabel      = nullptr;
    QLabel *m_warningLabel     = nullptr;
    QComboBox *m_spectrumCombo = nullptr;
    QComboBox *m_accuracyCombo = nullptr;
    QComboBox *m_lmaxCombo     = nullptr;
    QLabel *m_infoLabel        = nullptr;
    QComboBox *m_quantityCombo = nullptr; // what the y axis shows
    QCheckBox *m_logXCheck     = nullptr;
    QCheckBox *m_logYCheck     = nullptr;
    QToolButton *m_exportButton = nullptr;
    QPushButton *m_computeButton = nullptr;
    QProgressBar *m_progressBar  = nullptr;
    QLabel *m_progressLabel      = nullptr;
    bool m_plotLoaded          = false;
};
