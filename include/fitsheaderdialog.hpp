#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

// Shows the header cards of any HDU of a FITS file, with a filter box
class FitsHeaderDialog : public QDialog
{
    Q_OBJECT

public:
    FitsHeaderDialog(const QString &path, int hdu, QWidget *parent = nullptr);

private:
    void showHdu(int hdu);
    void applyFilter(const QString &text);

    QString m_path;
    QComboBox *m_hduCombo = nullptr;
    QLineEdit *m_filter   = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status      = nullptr;
};
