#include "statisticsdock.hpp"

#include "view.hpp"

#include <QFormLayout>
#include <QLabel>

StatisticsDock::StatisticsDock(QWidget *parent)
    : QDockWidget("Statistics", parent)
{
    setObjectName("StatisticsDock");

    auto *content = new QWidget();
    auto *form    = new QFormLayout(content);

    auto add = [form](const QString &name, QLabel *&label)
    {
        label = new QLabel("-");
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(name, label);
    };
    add("Nside:", m_nside);
    add("Npix:", m_npix);
    add("Min:", m_min);
    add("Max:", m_max);
    add("Average:", m_average);
    add("RMS:", m_rms);
    add("Std dev:", m_std);
    add("Sky fraction:", m_fsky);

    auto *scroll = new QWidget();
    auto *outer  = new QVBoxLayout(scroll);
    outer->addWidget(content);
    outer->addStretch();
    setWidget(scroll);
}

void
StatisticsDock::setView(const View *view)
{
    const OpenGLWidget *gl = view ? view->glWidget() : nullptr;
    if (!gl || !gl->healpixMap())
    {
        for (QLabel *l : {m_nside, m_npix, m_min, m_max, m_average, m_rms,
                          m_std, m_fsky})
            l->setText("-");
        return;
    }

    auto num = [](double v) { return QString::number(v, 'g', 6); };
    m_nside->setText(QString::number(gl->healpixMap()->Nside()));
    m_npix->setText(QString::number(gl->healpixMap()->Npix()));
    m_min->setText(num(gl->mapMin()));
    m_max->setText(num(gl->mapMax()));
    m_average->setText(num(gl->mapAverage()));
    m_rms->setText(num(gl->mapRms()));
    m_std->setText(num(gl->mapStd()));
    m_fsky->setText(QString::number(gl->fsky(), 'f', 6));
}
