#include "aboutcontent.hpp"

#include "aboutbanner.hpp"
#include "aboutinfo.hpp"

#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
// A tab that holds rich text with clickable links
QWidget *
textTab(const QString &html)
{
    auto *page   = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 14, 18, 8);

    auto *label = new QLabel(html);
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(label);
    layout->addStretch();
    return page;
}
} // namespace

AboutContent::AboutContent(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new AboutBanner());

    auto *tabs = new QTabWidget();
    tabs->setDocumentMode(true);
    tabs->addTab(textTab(aboutHtml()), "About");
    tabs->addTab(textTab(librariesHtml()), "Libraries");

    auto *license = new QPlainTextEdit(licenseText());
    license->setReadOnly(true);
    license->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    license->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    license->setFrameShape(QFrame::NoFrame);
    tabs->addTab(license, "License");

    layout->addWidget(tabs, 1);
}

QSize
AboutContent::sizeHint() const
{
    return {580, 500};
}
