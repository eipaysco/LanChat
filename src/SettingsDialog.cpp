#include "SettingsDialog.h"
#include "Settings.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("设置"));
    setMinimumWidth(420);

    Settings& s = Settings::instance();

    m_tcpPort = new QSpinBox(this); m_tcpPort->setRange(1, 65535); m_tcpPort->setValue(s.tcpPort());
    m_udpPort = new QSpinBox(this); m_udpPort->setRange(1, 65535); m_udpPort->setValue(s.udpPort());
    m_interval = new QSpinBox(this); m_interval->setRange(500, 60000); m_interval->setSuffix(" ms");
    m_interval->setValue(s.broadcastIntervalMs());
    m_machineName = new QLineEdit(s.machineName(), this);
    m_saveDir     = new QLineEdit(s.defaultSaveDir(), this);

    auto* browseBtn = new QPushButton(tr("浏览…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowseSaveDir);

    auto* saveDirRow = new QHBoxLayout;
    saveDirRow->addWidget(m_saveDir);
    saveDirRow->addWidget(browseBtn);

    auto* form = new QFormLayout;
    form->addRow(tr("TCP 监听端口（重启生效）:"), m_tcpPort);
    form->addRow(tr("UDP 发现端口（重启生效）:"), m_udpPort);
    form->addRow(tr("广播间隔:"), m_interval);
    form->addRow(tr("机器名:"), m_machineName);
    form->addRow(tr("默认保存目录:"), saveDirRow);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(btns);
}

void SettingsDialog::onBrowseSaveDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("选择保存目录"), m_saveDir->text());
    if (!dir.isEmpty()) m_saveDir->setText(dir);
}

void SettingsDialog::accept() {
    Settings& s = Settings::instance();
    s.setTcpPort(quint16(m_tcpPort->value()));
    s.setUdpPort(quint16(m_udpPort->value()));
    s.setBroadcastIntervalMs(m_interval->value());
    s.setMachineName(m_machineName->text().trimmed());
    s.setDefaultSaveDir(m_saveDir->text().trimmed());
    QDialog::accept();
}
