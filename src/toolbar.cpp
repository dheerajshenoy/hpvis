#include "toolbar.hpp"

#include <QLabel>

ToolBar::ToolBar(QWidget *parent) : QToolBar(parent)
{
    initUI();

    addWidget(new QLabel("Toolbar:"));
    // addAction(QIcon(":/icons/open.png"), "Open");
}

void
ToolBar::initUI()
{
    // Set the toolbar properties
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(24, 24));
}
