#pragma once

#include <QWidget>

class QLabel;

class StatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);
    void showMessage(const QString &message, int timeout = 0);

private:
    void initUI();

private:
    QLabel *m_messageLabel   = nullptr;
    QLabel *m_zoomLabel      = nullptr;
    QLabel *m_fileNameLabel  = nullptr;
    QLabel *m_cursorPosLabel = nullptr;
    QLabel *m_fileSizeLabel  = nullptr;
};
