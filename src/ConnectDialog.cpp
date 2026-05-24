#include "ConnectDialog.h"
#include "Settings.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

ConnectDialog::ConnectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("手动连接"));
    setMinimumWidth(320);

    m_ip = new QLineEdit(this);
    m_ip->setPlaceholderText("192.168.1.42");
    QRegularExpression ipRe(R"(^([0-9]{1,3}\.){3}[0-9]{1,3}$|^[A-Za-z0-9.\-]+$)");
    m_ip->setValidator(new QRegularExpressionValidator(ipRe, this));

    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(Settings::instance().tcpPort());

    auto* form = new QFormLayout;
    form->addRow(tr("IP 地址 / 主机名:"), m_ip);
    form->addRow(tr("端口:"), m_port);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText(tr("连接"));
    btns->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(btns);
}

QString ConnectDialog::host() const { return m_ip->text().trimmed(); }
quint16 ConnectDialog::port() const { return quint16(m_port->value()); }
