#include "exportdialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

ExportDialog::ExportDialog(const QSize &viewSize, QWidget *parent)
    : QDialog(parent), m_viewSize(viewSize.isValid() ? viewSize : QSize(1, 1))
{
    setWindowTitle("Export Image");

    m_title = new QLineEdit;
    m_title->setPlaceholderText("Optional");
    m_title->setClearButtonEnabled(true);

    m_format = new QComboBox;
    m_format->addItem("PNG", "png");
    m_format->addItem("JPEG", "jpg");
    m_format->addItem("BMP", "bmp");

    m_width = new QSpinBox;
    m_width->setRange(16, 16384);
    m_width->setSuffix(" px");
    m_width->setValue(m_viewSize.width());
    m_height = new QLabel;

    m_quality = new QSpinBox;
    m_quality->setRange(1, 100);
    m_quality->setValue(95);

    m_alpha = new QCheckBox("Keep transparent background");
    m_alpha->setToolTip("Only applies to PNG");

    auto *form = new QFormLayout;
    form->addRow("Title", m_title);
    form->addRow("Format", m_format);
    form->addRow("Width", m_width);
    form->addRow("Height", m_height);
    form->addRow("Quality", m_quality);
    form->addRow(QString(), m_alpha);

    auto *buttons
        = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Choose File...");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    auto updateEnabled = [this]()
    {
        const QString f = format();
        m_quality->setEnabled(f == "jpg");
        m_alpha->setEnabled(f == "png");
    };
    connect(m_format, &QComboBox::currentIndexChanged, this, updateEnabled);
    connect(m_width, &QSpinBox::valueChanged, this, &ExportDialog::updateHeight);
    updateEnabled();
    updateHeight();
}

void
ExportDialog::updateHeight()
{
    const int h = qRound(double(m_width->value()) * m_viewSize.height()
                         / m_viewSize.width());
    m_height->setText(QString("%1 px").arg(h));
}

QString
ExportDialog::format() const
{
    return m_format->currentData().toString();
}

ExportOptions
ExportDialog::options() const
{
    ExportOptions o;
    o.title        = m_title->text().trimmed();
    o.width        = m_width->value();
    o.quality      = format() == "jpg" ? m_quality->value() : -1;
    o.includeAlpha = m_alpha->isEnabled() && m_alpha->isChecked();
    return o;
}
