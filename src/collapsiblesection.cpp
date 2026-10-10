#include "collapsiblesection.hpp"

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *content,
                                       bool expanded, QWidget *parent)
    : QFrame(parent), m_content(content)
{
    setFrameShape(QFrame::StyledPanel);

    m_header = new QToolButton();
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setAutoRaise(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont f = m_header->font();
    f.setBold(true);
    m_header->setFont(f);

    m_content->setParent(this);
    m_content->setMaximumHeight(0);

    m_animation = new QPropertyAnimation(m_content, "maximumHeight", this);
    m_animation->setDuration(120);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_content);

    connect(m_header, &QToolButton::toggled, this,
            &CollapsibleSection::setExpanded);

    // Lift the height cap once fully open so the content can resize freely
    connect(m_animation, &QPropertyAnimation::finished, this, [this]()
    {
        if (m_header->isChecked())
            m_content->setMaximumHeight(QWIDGETSIZE_MAX);
    });

    m_header->setChecked(expanded);
    setExpanded(expanded);
    m_animation->stop();
    m_content->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
}

void
CollapsibleSection::setExpanded(bool expanded)
{
    if (m_header->isChecked() != expanded)
    {
        m_header->setChecked(expanded); // re-enters via toggled
        return;
    }

    m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);

    const int h = m_content->sizeHint().height();
    m_animation->stop();
    m_animation->setStartValue(expanded ? 0 : h);
    m_animation->setEndValue(expanded ? h : 0);
    m_animation->start();
}
