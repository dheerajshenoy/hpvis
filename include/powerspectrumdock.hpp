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
    };

    void initUI();
    void compute();
    void refresh();
    void updatePlot(const Spectra &spectra);

private:
    QPointer<View> m_view;
    QHash<const View *, Result> m_results;
    QSet<const View *> m_busy;

    QQuickView *m_chartView    = nullptr;
    QWidget *m_chartContainer  = nullptr;
    QLabel *m_statusLabel      = nullptr;
    QLabel *m_warningLabel     = nullptr;
    QComboBox *m_spectrumCombo = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_plotLoaded          = false;
};
