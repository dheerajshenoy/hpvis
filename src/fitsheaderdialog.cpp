#include "fitsheaderdialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <fitsio.h>

namespace
{
struct Card
{
    QString key, value, comment;
};

// Splits "KEY     = value / comment" (80 columns). COMMENT, HISTORY and blank
// cards have no value: their text goes in the comment column.
Card
parseCard(const QString &card)
{
    Card c;
    c.key = card.left(8).trimmed();
    if (card.mid(8, 2) != "= ")
    {
        c.comment = card.mid(8).trimmed();
        return c;
    }

    const QString rest = card.mid(10);
    bool inString      = false;
    int slash          = -1;
    for (int i = 0; i < rest.size(); ++i)
    {
        if (rest[i] == '\'')
            inString = !inString; // '' inside a string toggles twice
        else if (rest[i] == '/' && !inString)
        {
            slash = i;
            break;
        }
    }
    c.value   = (slash < 0 ? rest : rest.left(slash)).trimmed();
    c.comment = slash < 0 ? QString() : rest.mid(slash + 1).trimmed();
    return c;
}
} // namespace

FitsHeaderDialog::FitsHeaderDialog(const QString &path, int hdu,
                                   QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setWindowTitle(QString("FITS Header - %1").arg(QFileInfo(path).fileName()));
    resize(760, 520);

    m_hduCombo = new QComboBox();
    m_filter   = new QLineEdit();
    m_filter->setPlaceholderText("Filter keyword, value or comment...");
    m_filter->setClearButtonEnabled(true);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({"Keyword", "Value", "Comment"});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_status = new QLabel();

    auto *copy = new QPushButton("Copy");
    copy->setToolTip("Copy the selected cards (or all shown ones)");
    auto *close = new QPushButton("Close");
    close->setDefault(true);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel("HDU:"));
    top->addWidget(m_hduCombo);
    top->addWidget(m_filter, 1);

    auto *bottom = new QHBoxLayout();
    bottom->addWidget(m_status, 1);
    bottom->addWidget(copy);
    bottom->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    layout->addLayout(bottom);

    // List every HDU (including the primary one and images) so any header can
    // be inspected
    fitsfile *f = nullptr;
    int status  = 0;
    int count   = 0;
    if (fits_open_file(&f, path.toLocal8Bit().constData(), READONLY, &status)
        == 0)
    {
        fits_get_num_hdus(f, &count, &status);
        for (int i = 1; i <= count; ++i)
        {
            int type = 0;
            char extname[FLEN_VALUE] = "";
            int st2                  = 0;
            fits_movabs_hdu(f, i, &type, &st2);
            fits_read_key(f, TSTRING, "EXTNAME", extname, nullptr, &st2);
            QString label = QString::number(i);
            if (i == 1)
                label += ": primary";
            else if (extname[0])
                label += QString(": %1").arg(extname);
            m_hduCombo->addItem(label, i);
        }
        fits_close_file(f, &status);
    }

    connect(m_hduCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { showHdu(m_hduCombo->currentData().toInt()); });
    connect(m_filter, &QLineEdit::textChanged, this,
            &FitsHeaderDialog::applyFilter);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(copy, &QPushButton::clicked, this, [this]()
    {
        QStringList lines;
        QList<int> rows;
        for (const QModelIndex &i : m_table->selectionModel()->selectedRows())
            rows << i.row();
        if (rows.isEmpty())
            for (int r = 0; r < m_table->rowCount(); ++r)
                if (!m_table->isRowHidden(r))
                    rows << r;
        std::sort(rows.begin(), rows.end());
        for (int r : rows)
            lines << QString("%1 = %2 / %3")
                         .arg(m_table->item(r, 0)->text(),
                              m_table->item(r, 1)->text(),
                              m_table->item(r, 2)->text());
        QApplication::clipboard()->setText(lines.join('\n'));
    });

    const int row = m_hduCombo->findData(hdu);
    m_hduCombo->setCurrentIndex(row >= 0 ? row : 0);
    if (m_hduCombo->count() > 0)
        showHdu(m_hduCombo->currentData().toInt());
    else
        m_status->setText("Could not read the FITS file.");
}

void
FitsHeaderDialog::showHdu(int hdu)
{
    m_table->setRowCount(0);

    fitsfile *f = nullptr;
    int status  = 0;
    if (fits_open_file(&f, m_path.toLocal8Bit().constData(), READONLY, &status))
    {
        m_status->setText("Could not read the FITS file.");
        return;
    }

    int type = 0;
    int keys = 0, more = 0;
    if (fits_movabs_hdu(f, hdu, &type, &status)
        || fits_get_hdrspace(f, &keys, &more, &status))
    {
        m_status->setText("Could not read this HDU.");
        fits_close_file(f, &status);
        return;
    }

    m_table->setRowCount(keys);
    int rows = 0;
    for (int i = 1; i <= keys; ++i)
    {
        char card[FLEN_CARD] = "";
        if (fits_read_record(f, i, card, &status))
            break;
        const Card c = parseCard(QString::fromLatin1(card));
        if (c.key == "END")
            continue;
        m_table->setItem(rows, 0, new QTableWidgetItem(c.key));
        m_table->setItem(rows, 1, new QTableWidgetItem(c.value));
        m_table->setItem(rows, 2, new QTableWidgetItem(c.comment));
        ++rows;
    }
    m_table->setRowCount(rows);
    fits_close_file(f, &status);

    m_table->resizeColumnToContents(0);
    m_table->resizeColumnToContents(1);
    m_status->setText(QString("%1 cards").arg(rows));
    applyFilter(m_filter->text());
}

void
FitsHeaderDialog::applyFilter(const QString &text)
{
    int shown = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        bool match = text.isEmpty();
        for (int c = 0; c < 3 && !match; ++c)
            match = m_table->item(r, c)->text().contains(
                text, Qt::CaseInsensitive);
        m_table->setRowHidden(r, !match);
        shown += match;
    }
    m_status->setText(text.isEmpty()
                          ? QString("%1 cards").arg(m_table->rowCount())
                          : QString("%1 of %2 cards")
                                .arg(shown)
                                .arg(m_table->rowCount()));
}
