#pragma once

#include <QWidget>

class QLabel;

class StatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);
    void showMessage(const QString &message, int timeout = 0);

    void setZoom(float zoom);
    void setFilename(const QString &filename);
    void setCursorData(QPointF lonLat, double value, qlonglong index);
    void clear();
    void setFilesize(const QString &filesize);

private:
    void initUI();

private:
    QLabel *m_messageLabel    = nullptr;
    QLabel *m_zoomLabel       = nullptr;
    QLabel *m_fileNameLabel   = nullptr;
    QLabel *m_cursorPosLabel  = nullptr;
    QLabel *m_pixelIndexLabel = nullptr;
    QLabel *m_pixelValueLabel = nullptr;
    QLabel *m_fileSizeLabel   = nullptr;
};
