#include "statusbar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <cmath>

StatusBar::StatusBar(QWidget *parent) : QWidget(parent)
{
    initUI();
}

void
StatusBar::initUI()
{
    // Create a horizontal layout for the status bar
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 0, 5, 0);

    m_messageLabel    = new QLabel();
    m_zoomLabel       = new QLabel();
    m_fileNameLabel   = new QLabel();
    m_cursorPosLabel  = new QLabel();
    m_fileSizeLabel   = new QLabel();
    m_pixelValueLabel = new QLabel();
    m_pixelIndexLabel = new QLabel();

    layout->addWidget(m_messageLabel);
    layout->addWidget(m_fileNameLabel);
    layout->addWidget(m_fileSizeLabel);
    layout->addStretch();
    layout->addWidget(m_zoomLabel);
    layout->addWidget(m_cursorPosLabel);
    layout->addWidget(m_pixelIndexLabel);
    layout->addWidget(m_pixelValueLabel);

    // Set the layout for the status bar
    setLayout(layout);
}

void
StatusBar::showMessage(const QString &message, int timeout)
{
    m_messageLabel->setText(message);
    if (timeout > 0) // 0 = keep until replaced
        QTimer::singleShot(timeout, this,
                           [this]() { m_messageLabel->clear(); });
}

void
StatusBar::setZoom(float zoom)
{
    m_zoomLabel->setText(QString("Zoom: %1%").arg(qRound(zoom * 100)));
}

void
StatusBar::setFilename(const QString &filename)
{
    m_fileNameLabel->setText(filename);
}

void
StatusBar::setCursorData(QPointF lonLat, double value, qlonglong index)
{
    if (index < 0)
    {
        m_cursorPosLabel->clear();
        m_pixelIndexLabel->clear();
        m_pixelValueLabel->clear();
        return;
    }
    m_cursorPosLabel->setText(QString("Lon: %1°  Lat: %2°")
                                  .arg(lonLat.x(), 0, 'f', 2)
                                  .arg(lonLat.y(), 0, 'f', 2));
    m_pixelIndexLabel->setText(QString("Pixel: %1").arg(index));
    m_pixelValueLabel->setText(std::isfinite(value)
                                   ? QString("Value: %1").arg(value, 0, 'g', 6)
                                   : QString("Value: UNSEEN"));
}

void
StatusBar::setFilesize(const QString &filesize)
{
    m_fileSizeLabel->setText(QString("Size: %1").arg(filesize));
}

void
StatusBar::clear()
{
    m_messageLabel->clear();
    m_zoomLabel->clear();
    m_fileNameLabel->clear();
    m_cursorPosLabel->clear();
    m_pixelIndexLabel->clear();
    m_pixelValueLabel->clear();
    m_fileSizeLabel->clear();
}
