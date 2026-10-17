#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QListWidget;
class QTextBrowser;

// Help > Usage: the usage guide (docs/USAGE.md, embedded in the program). The
// guide is split at its "## " headings: the sections are listed on the left,
// the chosen one is shown on the right, and the search box keeps only the
// sections that mention the text.
class UsageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UsageDialog(QWidget *parent = nullptr);

    QSize sizeHint() const override;

private:
    struct Section
    {
        QString title;
        QString markdown; // the heading and its text
    };

    void loadGuide();
    void filterSections(const QString &text);
    void showSection(int row);

    QList<Section> m_sections;
    QLineEdit *m_search      = nullptr;
    QListWidget *m_list      = nullptr;
    QTextBrowser *m_browser  = nullptr;
};
