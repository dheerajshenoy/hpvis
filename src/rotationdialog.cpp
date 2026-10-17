#include "rotationdialog.hpp"

#include "openglwidget.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

RotationDialog::RotationDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Rotation");
    setWindowFlag(Qt::Tool); // stays above the main window, not modal

    auto *layout = new QVBoxLayout(this);
    auto *form   = new QFormLayout();
    layout->addLayout(form);

    m_lon  = addRow(form, "Center longitude:", -180.0, 180.0, true);
    m_lat  = addRow(form, "Center latitude:", -90.0, 90.0, false);
    m_roll = addRow(form, "Roll:", -180.0, 180.0, true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *reset   = buttons->addButton("Reset", QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, [this]()
    {
        if (m_gl)
            m_gl->resetRotation();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    setTarget(nullptr);
}

RotationDialog::Row
RotationDialog::addRow(QFormLayout *form, const QString &label, double min,
                       double max, bool wrap)
{
    Row row;
    row.slider = new QSlider(Qt::Horizontal);
    row.slider->setRange(int(min), int(max));
    row.slider->setMinimumWidth(180);

    row.spin = new QDoubleSpinBox();
    row.spin->setRange(min, max);
    row.spin->setDecimals(2);
    row.spin->setSingleStep(1.0);
    row.spin->setSuffix(QString::fromUtf8("°"));
    row.spin->setWrapping(wrap);

    auto *line = new QHBoxLayout();
    line->addWidget(row.slider, 1);
    line->addWidget(row.spin);
    form->addRow(label, line);

    // The slider moves in whole degrees; the box takes the exact value
    connect(row.slider, &QSlider::valueChanged, row.spin,
            [spin = row.spin](int v) { spin->setValue(v); });
    connect(row.spin, &QDoubleSpinBox::valueChanged, this, [this, row](double v)
    {
        {
            const QSignalBlocker block(row.slider);
            row.slider->setValue(int(std::lround(v)));
        }
        apply();
    });
    return row;
}

void
RotationDialog::setTarget(OpenGLWidget *gl)
{
    QObject::disconnect(m_connection);
    m_gl = gl;
    if (m_gl)
        m_connection = connect(m_gl, &OpenGLWidget::rotationChanged, this,
                               &RotationDialog::refresh);

    const bool on = m_gl != nullptr;
    for (Row *row : {&m_lon, &m_lat, &m_roll})
    {
        row->slider->setEnabled(on);
        row->spin->setEnabled(on);
    }
    refresh();
}

void
RotationDialog::apply()
{
    if (!m_gl)
        return;
    m_applying = true;
    m_gl->setRotationAngles(m_lon.spin->value(), m_lat.spin->value(),
                            m_roll.spin->value());
    m_applying = false;
}

void
RotationDialog::refresh()
{
    if (m_applying || !m_gl)
        return;
    double lon, lat, roll;
    m_gl->rotationAngles(lon, lat, roll);
    const struct
    {
        Row *row;
        double value;
    } values[] = {{&m_lon, lon}, {&m_lat, lat}, {&m_roll, roll}};
    for (const auto &[row, value] : values)
    {
        const QSignalBlocker b1(row->slider), b2(row->spin);
        row->spin->setValue(value);
        row->slider->setValue(int(std::lround(value)));
    }
    // In a frame the north pole stays up: there is no roll to set
    const bool free = !m_gl->orientationLocked();
    m_roll.slider->setEnabled(free);
    m_roll.spin->setEnabled(free);
}
