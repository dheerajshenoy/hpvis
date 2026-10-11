#pragma once

#include "fitsinfo.hpp"
#include "healpix_map.h"
#include "mapoperation.hpp"

#include <QDialog>
#include <QPointer>
#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class View;

// A map that is open in a tab, which can be used as an input
struct OpenMap
{
    QString title;
    QPointer<View> view;
};

// Combines several maps pixel by pixel with an expression (A - B, (A + B) / 2,
// ...) into a new map. The inputs are open tabs and/or FITS files.
class MapOperationDialog : public QDialog
{
    Q_OBJECT
public:
    // `openMaps` lists the tabs that can be used as inputs (asked each time)
    explicit MapOperationDialog(std::function<QList<OpenMap>()> openMaps,
                                QWidget *parent = nullptr);

signals:
    // The finished result; the main window opens it as a new tab
    void mapReady(std::shared_ptr<Healpix_Map<float>> map, const QString &title,
                  SkyFrame frame);

private:
    struct Row
    {
        QString name; // "A", "B", ...
        QString source;
        QPointer<View> view; // set for an open tab
        QString path;        // set for a file
        FitsInfo info;       // of the file
        QComboBox *hduCombo = nullptr;
        QComboBox *colCombo = nullptr;
    };

    void initUI();
    void addTab(const OpenMap &map);
    void addFile(const QString &path);
    void addRow(Row row);
    void removeSelected();
    void fillColumns(int row, int hdu, int column);
    QString nextName() const;
    QStringList names() const;
    void applyPreset(int preset);
    void compute();
    void setBusy(bool busy);
    void showMessage(const QString &text, bool error);
    std::vector<OperandSource> operands() const;

private:
    std::function<QList<OpenMap>()> m_openMaps;
    QList<Row> m_rows;

    QTableWidget *m_table      = nullptr;
    QComboBox *m_presetCombo   = nullptr;
    QLineEdit *m_expression    = nullptr;
    QLineEdit *m_title         = nullptr;
    QComboBox *m_policyCombo   = nullptr;
    QLabel *m_message          = nullptr;
    QProgressBar *m_progress   = nullptr;
    QPushButton *m_addTabButton = nullptr;
    QPushButton *m_addFileButton = nullptr;
    QPushButton *m_removeButton  = nullptr;
    QPushButton *m_computeButton = nullptr;
    bool m_busy = false;
};
