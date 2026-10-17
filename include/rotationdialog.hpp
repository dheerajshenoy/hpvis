#pragma once

#include <QDialog>
#include <QPointer>

class OpenGLWidget;
class QDoubleSpinBox;
class QSlider;
class QFormLayout;

// Where the view is centered (longitude, latitude) and how it is rolled, with
// sliders and boxes. It stays open next to the map, applies changes as you
// make them and follows the rotation you do with the mouse.
class RotationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RotationDialog(QWidget *parent = nullptr);

    // The map to control (null: nothing to control, the controls are disabled)
    void setTarget(OpenGLWidget *gl);

private:
    struct Row
    {
        QSlider *slider      = nullptr;
        QDoubleSpinBox *spin = nullptr;
    };
    Row addRow(QFormLayout *form, const QString &label, double min, double max,
               bool wrap);
    void apply();   // controls -> view
    void refresh(); // view -> controls

    QPointer<OpenGLWidget> m_gl;
    QMetaObject::Connection m_connection;
    bool m_applying = false;
    Row m_lon, m_lat, m_roll;
};
