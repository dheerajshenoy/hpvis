#pragma once

#include <QFrame>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QVBoxLayout>

// A header button with an arrow that expands/collapses a content widget.
class CollapsibleSection : public QFrame
{
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString &title, QWidget *content,
                                bool expanded   = true,
                                QWidget *parent = nullptr);

    inline bool isExpanded() const
    {
        return m_header->isChecked();
    }

    void setExpanded(bool expanded);

private:
    QToolButton *m_header           = nullptr;
    QWidget *m_content              = nullptr;
    QPropertyAnimation *m_animation = nullptr;
};
