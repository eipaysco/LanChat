#include "MainWindow.h"
#include "ConnectDialog.h"
#include "Discovery.h"
#include "DropArea.h"
#include "PeerConnection.h"
#include "PeerServer.h"
#include "Settings.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTcpSocket>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QString nowStr() { return QDateTime::currentDateTime().toString("HH:mm:ss"); }

QString uniqueTargetPath(const QString& dir, const QString& fileName) {
    QString target = QDir(dir).filePath(fileName);
    QFileInfo original(target);
    const QString baseName = original.completeBaseName();
    const QString suffix = original.suffix();

    int n = 1;
    while (QFile::exists(target)) {
        const QString numberedName = suffix.isEmpty()
            ? QString("%1 (%2)").arg(baseName).arg(n++)
            : QString("%1 (%2).%3").arg(baseName).arg(n++).arg(suffix);
        target = QDir(dir).filePath(numberedName);
    }
    return target;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("LanChat");
    resize(900, 600);

    buildUi();
    startNetwork();
    setConnectedState(false);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* e) {
    if (m_discovery) m_discovery->sayBye();
    if (m_conn) m_conn->disconnectFromPeer();
    e->accept();
}

QString MainWindow::humanSize(qint64 bytes) {
    const double KB = 1024.0, MB = KB * 1024, GB = MB * 1024;
    if (bytes >= GB) return QString::asprintf("%.2f GB", bytes / GB);
    if (bytes >= MB) return QString::asprintf("%.2f MB", bytes / MB);
    if (bytes >= KB) return QString::asprintf("%.1f KB", bytes / KB);
    return QString::number(bytes) + " B";
}

// ───────────────────────────────────────────────── UI build

void MainWindow::buildUi() {
    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    auto* actManualConnect = fileMenu->addAction(tr("手动连接..."));
    auto* actSettings      = fileMenu->addAction(tr("设置..."));
    fileMenu->addSeparator();
    auto* actQuit          = fileMenu->addAction(tr("退出"));
    connect(actManualConnect, &QAction::triggered, this, &MainWindow::onManualConnect);
    connect(actSettings,      &QAction::triggered, this, &MainWindow::onOpenSettings);
    connect(actQuit,          &QAction::triggered, this, &QWidget::close);

    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    auto* actAbout = helpMenu->addAction(tr("关于"));
    connect(actAbout, &QAction::triggered, this, [this]{
        QMessageBox::about(this, "LanChat", "LanChat — 局域网通讯工具");
    });

    m_peerList = new QListWidget(this);
    m_peerList->setMinimumWidth(220);
    connect(m_peerList, &QListWidget::itemDoubleClicked, this, &MainWindow::onPeerDoubleClicked);

    auto* manualBtn = new QPushButton(tr("手动连接..."), this);
    connect(manualBtn, &QPushButton::clicked, this, &MainWindow::onManualConnect);

    auto* leftWrap = new QWidget(this);
    auto* leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0,0,0,0);
    leftLay->addWidget(new QLabel(tr("已发现对端"), this));
    leftLay->addWidget(m_peerList);
    leftLay->addWidget(manualBtn);

    m_statusTop = new QLabel(tr("未连接"), this);
    m_statusTop->setStyleSheet("padding:4px; background:#eee; border-radius:4px;");

    m_disconnectBtn = new QPushButton(tr("断开"), this);
    m_disconnectBtn->setEnabled(false);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);

    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(m_statusTop, 1);
    statusRow->addWidget(m_disconnectBtn);

    m_chat = new QTextEdit(this);
    m_chat->setReadOnly(true);

    m_transfers = new QListWidget(this);
    m_transfers->setMaximumHeight(120);

    m_dropArea = new DropArea(this);
    connect(m_dropArea, &DropArea::filesDropped, this, &MainWindow::onFilesDropped);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("输入消息，回车发送"));
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);

    m_sendBtn   = new QPushButton(tr("发送"), this);
    m_chooseBtn = new QPushButton(tr("选文件..."), this);
    connect(m_sendBtn,   &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_chooseBtn, &QPushButton::clicked, this, &MainWindow::onChooseFileClicked);

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendBtn);
    inputRow->addWidget(m_chooseBtn);

    auto* rightWrap = new QWidget(this);
    auto* rightLay = new QVBoxLayout(rightWrap);
    rightLay->setContentsMargins(0,0,0,0);
    rightLay->addLayout(statusRow);
    rightLay->addWidget(m_chat, 1);
    rightLay->addWidget(new QLabel(tr("文件传输"), this));
    rightLay->addWidget(m_transfers);
    rightLay->addWidget(m_dropArea);
    rightLay->addLayout(inputRow);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(leftWrap);
    splitter->addWidget(rightWrap);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_statusBottom = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusBottom, 1);

    QStringList ips;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                ips << e.ip().toString();
        }
    }
    m_statusBottom->setText(QString("本机: %1 | IP: %2 | TCP %3 UDP %4")
        .arg(Settings::instance().machineName())
        .arg(ips.join(", "))
        .arg(Settings::instance().tcpPort())
        .arg(Settings::instance().udpPort()));
}

void MainWindow::startNetwork() {
    m_discovery = new Discovery(this);
    connect(m_discovery, &Discovery::peerSeen, this, &MainWindow::onPeerSeen);
    connect(m_discovery, &Discovery::peerLost, this, &MainWindow::onPeerLost);
    if (!m_discovery->start()) {
        appendLog(QString("<span style='color:red'>[警告] UDP 发现启动失败（端口 %1 被占用？）</span>")
                  .arg(Settings::instance().udpPort()));
    }

    m_server = new PeerServer(this);
    connect(m_server, &PeerServer::newPeerConnection, this, &MainWindow::onNewIncomingConnection);
    connect(m_server, &PeerServer::listenError, this, [this](const QString& msg){
        appendLog("<span style='color:red'>[错误] TCP 监听失败: " + msg.toHtmlEscaped() + "</span>");
    });
    m_server->start(Settings::instance().tcpPort());
}

void MainWindow::appendLog(const QString& html) {
    m_chat->append("<span style='color:#888'>[" + nowStr() + "]</span> " + html);
}

void MainWindow::setConnectedState(bool connected, const QString& remoteDesc) {
    if (connected) {
        m_statusTop->setText(tr("已连接：%1").arg(remoteDesc));
        m_statusTop->setStyleSheet("padding:4px; background:#d6f5d6; border-radius:4px;");
        m_disconnectBtn->setEnabled(true);
        m_dropArea->setEnabledForDrop(true);
        m_sendBtn->setEnabled(true);
        m_chooseBtn->setEnabled(true);
    } else {
        m_statusTop->setText(tr("未连接"));
        m_statusTop->setStyleSheet("padding:4px; background:#eee; border-radius:4px;");
        m_disconnectBtn->setEnabled(false);
        m_dropArea->setEnabledForDrop(false);
        m_sendBtn->setEnabled(false);
        m_chooseBtn->setEnabled(false);
    }
}

QListWidgetItem* MainWindow::itemForTransfer(const QString& id) {
    return m_transferItems.value(id, nullptr);
}

QListWidgetItem* MainWindow::createTransferItem(const QString& id, const QString& text) {
    auto* it = new QListWidgetItem(text, m_transfers);
    m_transferItems.insert(id, it);
    return it;
}

void MainWindow::replaceConnection() {
    if (m_conn) {
        m_conn->disconnect(this);
        m_conn->disconnectFromPeer();
        m_conn->deleteLater();
    }
    m_conn = new PeerConnection(this);
    wireConnectionSignals();
}

void MainWindow::wireConnectionSignals() {
    connect(m_conn, &PeerConnection::connected,      this, &MainWindow::onConnConnected);
    connect(m_conn, &PeerConnection::disconnected,   this, &MainWindow::onConnDisconnected);
    connect(m_conn, &PeerConnection::errorOccurred,  this, &MainWindow::onConnError);
    connect(m_conn, &PeerConnection::textReceived,   this, &MainWindow::onConnText);
    connect(m_conn, &PeerConnection::fileOffered,    this, &MainWindow::onConnFileOffered);
    connect(m_conn, &PeerConnection::fileProgress,   this, &MainWindow::onConnFileProgress);
    connect(m_conn, &PeerConnection::fileFinished,   this, &MainWindow::onConnFileFinished);
    connect(m_conn, &PeerConnection::fileError,      this, &MainWindow::onConnFileError);
}

// ───────────────────────────────────────────────── slot handlers

void MainWindow::onManualConnect() {
    ConnectDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (dlg.host().isEmpty()) return;

    appendLog(QString("正在连接 %1:%2 ...").arg(dlg.host()).arg(dlg.port()));
    replaceConnection();
    m_conn->connectToPeer(dlg.host(), dlg.port());
}

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::onPeerDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    QString peerId = item->data(Qt::UserRole).toString();
    if (peerId.isEmpty() || !m_discovery) return;

    for (const DiscoveredPeer& p : m_discovery->peers()) {
        if (p.peerId != peerId) continue;
        appendLog(QString("正在连接 %1 (%2:%3) ...").arg(p.machineName, p.address.toString()).arg(p.tcpPort));
        replaceConnection();
        m_conn->connectToPeer(p.address.toString(), p.tcpPort);
        return;
    }
}

void MainWindow::onSendClicked() {
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_conn || !m_conn->isConnected()) return;
    m_conn->sendText(text);
    appendLog("<b>我:</b> " + text.toHtmlEscaped());
    m_input->clear();
}

void MainWindow::onChooseFileClicked() {
    if (!m_conn || !m_conn->isConnected()) return;
    QStringList files = QFileDialog::getOpenFileNames(this, tr("选择要发送的文件"));
    if (files.isEmpty()) return;
    onFilesDropped(files);
}

void MainWindow::onDisconnectClicked() {
    if (m_conn) m_conn->disconnectFromPeer();
}

void MainWindow::onFilesDropped(const QStringList& paths) {
    if (!m_conn || !m_conn->isConnected()) {
        QMessageBox::information(this, "LanChat", tr("请先连接一个对端"));
        return;
    }
    for (const QString& p : paths) {
        QFileInfo fi(p);
        appendLog(QString("发起发送 %1 (%2)").arg(fi.fileName().toHtmlEscaped(), humanSize(fi.size())));
        m_conn->sendFile(p);
    }
}

// ───── discovery

void MainWindow::onPeerSeen(const DiscoveredPeer& p) {
    auto* it = m_peerItems.value(p.peerId, nullptr);
    QString label = QString("● %1\n   %2:%3").arg(p.machineName, p.address.toString()).arg(p.tcpPort);
    if (!it) {
        it = new QListWidgetItem(label, m_peerList);
        it->setData(Qt::UserRole, p.peerId);
        m_peerItems.insert(p.peerId, it);
    } else {
        it->setText(label);
    }
}

void MainWindow::onPeerLost(const QString& peerId) {
    auto it = m_peerItems.find(peerId);
    if (it == m_peerItems.end()) return;
    delete it.value();
    m_peerItems.erase(it);
}

// ───── server-side incoming

void MainWindow::onNewIncomingConnection(QTcpSocket* socket) {
    appendLog(QString("收到来自 %1 的连接").arg(socket->peerAddress().toString()));
    replaceConnection();
    m_conn->adoptSocket(socket);
}

// ───── PeerConnection signals

void MainWindow::onConnConnected() {
    if (!m_conn) return;
    setConnectedState(true, m_conn->remoteName() + " (" + m_conn->remoteAddress() + ")");
    appendLog(QString("<span style='color:green'>已连接到 %1</span>").arg(m_conn->remoteName().toHtmlEscaped()));
}

void MainWindow::onConnDisconnected() {
    setConnectedState(false);
    appendLog(QString("<span style='color:#888'>连接已断开</span>"));
}

void MainWindow::onConnError(const QString& msg) {
    appendLog(QString("<span style='color:red'>错误: %1</span>").arg(msg.toHtmlEscaped()));
}

void MainWindow::onConnText(const QString& msg) {
    QString from = m_conn ? m_conn->remoteName() : "?";
    appendLog("<b>" + from.toHtmlEscaped() + ":</b> " + msg.toHtmlEscaped());
}

void MainWindow::onConnFileOffered(const QString& id, const QString& name, qint64 size) {
    QString from = m_conn ? m_conn->remoteName() : "?";
    auto choice = QMessageBox::question(this, tr("收到文件"),
        QString("%1 想发送文件:\n\n%2 (%3)\n\n接收吗？").arg(from, name, humanSize(size)));
    if (choice != QMessageBox::Yes) {
        if (m_conn) m_conn->respondFileOffer(id, false);
        return;
    }
    QString dir = Settings::instance().defaultSaveDir();
    QDir().mkpath(dir);
    QString target = uniqueTargetPath(dir, name);
    QString picked = QFileDialog::getSaveFileName(this, tr("保存为"), target);
    if (picked.isEmpty()) {
        if (m_conn) m_conn->respondFileOffer(id, false);
        return;
    }
    createTransferItem(id, QString("← 接收 %1 (%2)  0%").arg(name, humanSize(size)));
    if (m_conn) m_conn->respondFileOffer(id, true, picked);
}

void MainWindow::onConnFileProgress(const QString& id, qint64 bytes, qint64 total, bool sending) {
    auto* it = itemForTransfer(id);
    if (!it) {
        QString prefix = sending ? "→ 发送" : "← 接收";
        it = createTransferItem(id, QString("%1  ...").arg(prefix));
    }
    int pct = total > 0 ? int(100.0 * bytes / total) : 0;
    QString prefix = sending ? "→ 发送" : "← 接收";
    it->setText(QString("%1 %2 / %3  (%4%)").arg(prefix, humanSize(bytes), humanSize(total)).arg(pct));
}

void MainWindow::onConnFileFinished(const QString& id, const QString& path, bool sending) {
    auto* it = itemForTransfer(id);
    QFileInfo fi(path);
    QString line = sending
        ? QString("→ 发送完成: %1").arg(fi.fileName())
        : QString("← 接收完成: %1 (已保存到 %2)").arg(fi.fileName(), QDir::toNativeSeparators(path));
    if (it) it->setText(line);
    else    createTransferItem(id, line);
    appendLog(line.toHtmlEscaped());
}

void MainWindow::onConnFileError(const QString& id, const QString& reason) {
    auto* it = itemForTransfer(id);
    QString line = QString("传输失败: %1").arg(reason);
    if (it) it->setText(line);
    appendLog("<span style='color:red'>" + line.toHtmlEscaped() + "</span>");
}
