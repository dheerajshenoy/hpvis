#pragma once

#include "pixelordering.hpp"

#include <QComboBox>
#include <QDockWidget>
#include <QPushButton>
#include <QVBoxLayout>

class SidePanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit SidePanel(QWidget *parent = nullptr);
    void setPixelOrdering(PixelOrdering ordering);
    inline PixelOrdering pixelOrdering()
    {
        return m_pixOrdCombo->currentIndex() == (int)PixelOrdering::NESTED
                   ? PixelOrdering::NESTED
                   : PixelOrdering::RING;
    }

signals:
    void pixelOrderingChanged(PixelOrdering ordering);

private:
    void setupUI();
    void setupConnections();

private:
    QComboBox *m_pixOrdCombo = nullptr;
};
