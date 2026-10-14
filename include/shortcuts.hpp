#pragma once

#include <QKeySequence>
#include <QString>

// A command the user can bind a key to. The id is stable (it is the key the
// binding is stored under): the menu path for menu actions, a fixed name for
// the viewer's own keys.
struct ShortcutCommand
{
    QString id;
    QString label;    // e.g. "Zoom > Reset Zoom"
    QString category; // top level menu, or the viewer keys
    QKeySequence defaultKey;
};
