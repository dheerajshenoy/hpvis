#include "usagedialog.hpp"

#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

UsageDialog::UsageDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("hpvis Usage");

    auto *layout = new QVBoxLayout(this);

    m_search = new QLineEdit();
    m_search->setPlaceholderText("Search the guide...");
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    auto *row = new QHBoxLayout();
    m_list    = new QListWidget();
    m_list->setFixedWidth(220);
    m_browser = new QTextBrowser();
    m_browser->setOpenExternalLinks(true);
    row->addWidget(m_list);
    row->addWidget(m_browser, 1);
    layout->addLayout(row, 1);

    loadGuide();
    filterSections(QString());

    connect(m_search, &QLineEdit::textChanged, this,
            &UsageDialog::filterSections);
    connect(m_list, &QListWidget::currentRowChanged, this,
            &UsageDialog::showSection);
}

QSize
UsageDialog::sizeHint() const
{
    return {960, 680};
}

// Reads the guide and cuts it into sections at the "## " headings; what comes
// before the first one is the introduction
void
UsageDialog::loadGuide()
{
    QFile file(QStringLiteral(":/docs/USAGE.md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_sections.append(
            Section{"Usage", "The usage guide could not be loaded."});
        return;
    }
    const QStringList lines
        = QString::fromUtf8(file.readAll()).split('\n');

    bool inCode = false; // a "## " inside a code block is not a heading
    Section current{"Introduction", QString()};
    for (const QString &line : lines)
    {
        if (line.startsWith("```"))
            inCode = !inCode;
        if (!inCode && line.startsWith("## "))
        {
            if (!current.markdown.trimmed().isEmpty())
                m_sections.append(current);
            current = Section{line.mid(3).trimmed(), line + QChar(0x0A)};
        }
        else
            current.markdown += line + '\n';
    }
    if (!current.markdown.trimmed().isEmpty())
        m_sections.append(current);
}

// Lists the sections that contain `text` (all of them when it is empty)
void
UsageDialog::filterSections(const QString &text)
{
    const QString wanted = text.trimmed();
    m_list->clear();
    for (int i = 0; i < m_sections.size(); ++i)
    {
        const Section &s = m_sections[i];
        if (!wanted.isEmpty()
            && !s.title.contains(wanted, Qt::CaseInsensitive)
            && !s.markdown.contains(wanted, Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(s.title, m_list);
        item->setData(Qt::UserRole, i);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        m_browser->setPlainText("No section mentions that.");
}

void
UsageDialog::showSection(int row)
{
    if (row < 0)
        return;
    const int index = m_list->item(row)->data(Qt::UserRole).toInt();
    m_browser->setMarkdown(m_sections[index].markdown);

    // Show where the search text is in the section
    const QString wanted = m_search->text().trimmed();
    if (!wanted.isEmpty())
        m_browser->find(wanted);
    else
        m_browser->moveCursor(QTextCursor::Start);
}
