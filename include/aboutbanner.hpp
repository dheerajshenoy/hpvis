#pragma once

#include <QWidget>

class QTimer;

// Banner of the About dialogs: a slowly turning conical rainbow, and the name
// of the program at the bottom left.
// The animation only runs while the banner is visible.
class AboutBanner : public QWidget
{
    Q_OBJECT
public:
    explicit AboutBanner(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QTimer *m_timer = nullptr;
    qreal m_angle   = 0.0; // degrees: how far the rainbow has turned
};
