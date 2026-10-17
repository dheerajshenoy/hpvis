#pragma once

#include <QWidget>

// The body of Help > About and of the About page in the settings: the banner
// over three tabs (About, Libraries, License)
class AboutContent : public QWidget
{
    Q_OBJECT
public:
    explicit AboutContent(QWidget *parent = nullptr);

    QSize sizeHint() const override;
};
