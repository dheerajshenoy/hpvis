#pragma once

#include <QPointer>
#include <QString>

class View;

// A map that is open in a tab
struct OpenMap
{
    QString title;
    QPointer<View> view;
};
