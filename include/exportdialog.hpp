#pragma once

#include "exportoptions.hpp"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;

// Asks for the title and the settings of an image export before the file
// dialog is shown
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    // `viewSize` is the size of the map as shown, to preselect the dimensions
    explicit ExportDialog(const QSize &viewSize, QWidget *parent = nullptr);

    ExportOptions options() const;
    // File extension of the chosen format, without the dot
    QString format() const;

private:
    void updateHeight();

    QSize m_viewSize;
    QLineEdit *m_title;
    QComboBox *m_format;
    QSpinBox *m_width;
    QLabel *m_height;
    QSpinBox *m_quality;
    QCheckBox *m_alpha;
};
