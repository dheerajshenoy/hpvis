#pragma once

#include <QDialog>

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onSaveClicked();

private:
    void initUI();
    void initConnections();
    void loadSettings();
    void saveSettings();
};
