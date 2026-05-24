#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(QWidget* parent = nullptr);

    QString host() const;
    quint16 port() const;

private:
    QLineEdit* m_ip   = nullptr;
    QSpinBox*  m_port = nullptr;
};
