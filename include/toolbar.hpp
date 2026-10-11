#pragma once

#include <QToolBar>

class ToolBar : public QToolBar
{
    Q_OBJECT

public:
    explicit ToolBar(QWidget *parent = nullptr);

private:
    void initUI();
};
