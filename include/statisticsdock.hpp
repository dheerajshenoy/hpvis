#pragma once

#include <QDockWidget>

class QFormLayout;
class QLabel;
class View;

// Dockable map statistics. Follows the current tab and its masks.
class StatisticsDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit StatisticsDock(QWidget *parent = nullptr);

    // Shows the statistics of `view` (clears them when null)
    void setView(const View *view);

private:
    QLabel *m_nside    = nullptr;
    QLabel *m_npix     = nullptr;
    QLabel *m_min      = nullptr;
    QLabel *m_max      = nullptr;
    QLabel *m_average  = nullptr;
    QLabel *m_rms      = nullptr;
    QLabel *m_std      = nullptr;
    QLabel *m_fsky     = nullptr;
};
