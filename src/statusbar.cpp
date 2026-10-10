#include "statusbar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

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

    m_messageLabel   = new QLabel();
    m_zoomLabel      = new QLabel();
    m_fileNameLabel  = new QLabel();
    m_cursorPosLabel = new QLabel();
    m_fileSizeLabel  = new QLabel();

    layout->addWidget(m_messageLabel);
    layout->addWidget(m_fileNameLabel);
    layout->addWidget(m_fileSizeLabel);
    layout->addStretch();
    layout->addWidget(m_zoomLabel);
    layout->addWidget(m_cursorPosLabel);

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
