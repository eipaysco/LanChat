#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

protected:
    void accept() override;

private slots:
    void onBrowseSaveDir();

private:
    QSpinBox*  m_tcpPort     = nullptr;
    QSpinBox*  m_udpPort     = nullptr;
    QSpinBox*  m_interval    = nullptr;
    QLineEdit* m_machineName = nullptr;
    QLineEdit* m_saveDir     = nullptr;
};
