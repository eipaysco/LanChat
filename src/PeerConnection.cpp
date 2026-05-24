#include "PeerConnection.h"
#include "Protocol.h"
#include "Settings.h"

#include <QDataStream>
#include <QFileDevice>
#include <QFileInfo>
#include <QHostAddress>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include <functional>

using Protocol::CHUNK_SIZE;
using Protocol::WRITE_BUFFER_LIMIT;

namespace {

QByteArray packDataStream(std::function<void(QDataStream&)> fn) {
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_5_14);
    fn(ds);
    return buf;
}

QString sanitizedFileName(const QString& rawName) {
    QString normalized = rawName.trimmed();
    normalized.replace('\\', '/');

    QString name = QFileInfo(normalized).fileName().trimmed();
    QString cleaned;
    cleaned.reserve(name.size());

    for (const QChar& ch : name) {
        const ushort code = ch.unicode();
        if (code < 32 || ch == '/' || ch == '\\' || ch == ':' || ch == '*'
            || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            cleaned.append('_');
        } else {
            cleaned.append(ch);
        }
    }

    while (cleaned.endsWith('.') || cleaned.endsWith(' ')) cleaned.chop(1);
    if (cleaned.isEmpty() || cleaned == "." || cleaned == "..") return "received-file";
    return cleaned;
}

} // namespace

PeerConnection::PeerConnection(QObject* parent) : QObject(parent) {
    m_helloTimer = new QTimer(this);
    m_helloTimer->setSingleShot(true);
    m_helloTimer->setInterval(10000);
    connect(m_helloTimer, &QTimer::timeout, this, &PeerConnection::onHelloTimeout);
}

PeerConnection::~PeerConnection() {
    qDeleteAll(m_outFiles);
    qDeleteAll(m_inFiles);
}

void PeerConnection::adoptSocket(QTcpSocket* socket) {
    if (socket) socket->setParent(this);
    setupSocket(socket);
    // Socket is already TCP-connected; jump straight to HELLO.
    QTimer::singleShot(0, this, &PeerConnection::onConnected);
}

void PeerConnection::connectToPeer(const QString& host, quint16 port) {
    auto* s = new QTcpSocket(this);
    setupSocket(s);
    s->connectToHost(host, port);
}

void PeerConnection::disconnectFromPeer() {
    if (!m_socket) return;
    m_socket->disconnectFromHost();
}

bool PeerConnection::isConnected() const {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState && m_emittedConnected;
}

QString PeerConnection::remoteAddress() const {
    if (!m_socket) return {};
    return QString("%1:%2").arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort());
}

void PeerConnection::setupSocket(QTcpSocket* s) {
    if (m_socket && m_socket != s) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
    }
    m_socket = s;
    m_buffer.clear();
    m_helloSent = false;
    m_helloReceived = false;
    m_remotePeerId.clear();
    m_remoteName.clear();
    m_emittedConnected = false;
    if (!s) return;

    connect(s, &QTcpSocket::connected,    this, &PeerConnection::onConnected);
    connect(s, &QTcpSocket::readyRead,    this, &PeerConnection::onReadyRead);
    connect(s, &QTcpSocket::disconnected, this, &PeerConnection::onDisconnected);
    connect(s, &QTcpSocket::bytesWritten, this, &PeerConnection::onBytesWritten);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(s, &QAbstractSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError){ onSocketError(); });
#else
    // QAbstractSocket::error is both a signal (void error(SocketError)) and a const getter
    // (SocketError error() const) in Qt 5.14. QOverload fails to disambiguate on some
    // compilers (especially MinGW). Use an explicit static_cast to pick the signal.
    using SocketErrorSignal = void (QAbstractSocket::*)(QAbstractSocket::SocketError);
    connect(s, static_cast<SocketErrorSignal>(&QAbstractSocket::error),
            this, [this](QAbstractSocket::SocketError){ onSocketError(); });
#endif
}

void PeerConnection::onSocketError() {
    if (!m_socket) return;
    emit errorOccurred("Socket error: " + m_socket->errorString());
}

void PeerConnection::onConnected() {
    sendHello();
    m_helloTimer->start();
    if (m_socket && m_socket->bytesAvailable() > 0) onReadyRead();
}

void PeerConnection::onDisconnected() {
    m_helloTimer->stop();
    cleanupTransfers("connection lost");
    if (m_emittedConnected) emit disconnected();
    m_emittedConnected = false;
}

void PeerConnection::onHelloTimeout() {
    emit errorOccurred("HELLO timeout — peer did not greet within 10s");
    if (m_socket) m_socket->abort();
}

void PeerConnection::sendHello() {
    if (m_helloSent) return;
    QByteArray payload = packDataStream([](QDataStream& ds){
        ds << Settings::instance().peerId()
           << Settings::instance().machineName()
           << QString::fromLatin1(Protocol::APP_VERSION);
    });
    writeFrame(Protocol::HELLO, payload);
    m_helloSent = true;
}

void PeerConnection::writeFrame(quint8 type, const QByteArray& payload) {
    if (!m_socket) return;
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_5_14);
    ds << quint32(payload.size() + 1);
    ds << type;
    frame.append(payload);
    m_socket->write(frame);
}

void PeerConnection::onReadyRead() {
    if (!m_socket) return;
    m_buffer.append(m_socket->readAll());
    processFrames();
}

void PeerConnection::processFrames() {
    while (true) {
        if (m_buffer.size() < 4) return;
        quint32 frameLen = 0;
        {
            QDataStream ds(m_buffer);
            ds.setVersion(QDataStream::Qt_5_14);
            ds >> frameLen;
        }
        if (frameLen > 256u * 1024 * 1024) {
            emit errorOccurred("oversized frame, aborting");
            if (m_socket) m_socket->abort();
            m_buffer.clear();
            return;
        }
        if (m_buffer.size() < int(4 + frameLen)) return;

        QByteArray body = m_buffer.mid(4, int(frameLen));
        m_buffer.remove(0, int(4 + frameLen));

        if (body.isEmpty()) continue;
        quint8 type = quint8(body.at(0));
        QByteArray payload = body.mid(1);
        handleFrame(type, payload);
    }
}

void PeerConnection::handleFrame(quint8 type, const QByteArray& payload) {
    QDataStream ds(payload);
    ds.setVersion(QDataStream::Qt_5_14);

    switch (type) {
    case Protocol::HELLO: {
        QString peerId, name, version;
        ds >> peerId >> name >> version;
        if (ds.status() != QDataStream::Ok || peerId.isEmpty()) {
            emit errorOccurred("malformed HELLO frame");
            break;
        }
        m_remotePeerId  = peerId;
        m_remoteName    = name;
        m_helloReceived = true;
        m_helloTimer->stop();
        if (!m_emittedConnected) {
            m_emittedConnected = true;
            emit connected();
        }
        break;
    }
    case Protocol::TEXT: {
        QString msg;
        ds >> msg;
        if (ds.status() != QDataStream::Ok) {
            emit errorOccurred("malformed TEXT frame");
            break;
        }
        emit textReceived(msg);
        break;
    }
    case Protocol::FILE_OFFER: {
        QString id, name; qint64 size = 0;
        ds >> id >> name >> size;
        if (ds.status() != QDataStream::Ok || id.isEmpty() || size < 0) {
            if (!id.isEmpty()) sendFileReject(id, "invalid file offer");
            emit errorOccurred("invalid file offer");
            break;
        }
        if (m_inFiles.contains(id)) {
            sendFileReject(id, "duplicate transfer id");
            break;
        }
        InFile* in = new InFile;
        in->transferId = id;
        in->name       = sanitizedFileName(name);
        in->size       = size;
        m_inFiles.insert(id, in);
        emit fileOffered(id, in->name, size);
        break;
    }
    case Protocol::FILE_ACCEPT: {
        QString id; ds >> id;
        if (ds.status() != QDataStream::Ok) break;
        auto it = m_outFiles.find(id);
        if (it == m_outFiles.end()) break;
        OutFile* f = it.value();
        if (f->accepted) break;
        f->accepted = true;
        m_sendQueue.append(id);
        if (m_currentSendingId.isEmpty()) startNextOutgoing();
        break;
    }
    case Protocol::FILE_REJECT: {
        QString id, reason; ds >> id >> reason;
        if (ds.status() != QDataStream::Ok) break;
        failOutgoingTransfer(id, reason.isEmpty() ? QString("rejected by peer") : reason);
        break;
    }
    case Protocol::FILE_CHUNK: {
        QString id; QByteArray data;
        ds >> id >> data;
        if (ds.status() != QDataStream::Ok) break;
        auto it = m_inFiles.find(id);
        if (it == m_inFiles.end()) break;
        InFile* in = it.value();
        if (!in->file.isOpen()) {
            failIncomingTransfer(id, "received file data before acceptance", true);
            break;
        }
        if (data.size() > CHUNK_SIZE || in->received + data.size() > in->size) {
            failIncomingTransfer(id, "received more data than advertised", true);
            break;
        }
        qint64 written = in->file.write(data);
        if (written != data.size()) {
            QString reason = in->file.errorString();
            if (reason.isEmpty() || reason == "Unknown error") reason = "file write failed";
            failIncomingTransfer(id, reason, true);
            break;
        }
        in->received += written;
        emit fileProgress(id, in->received, in->size, false);
        break;
    }
    case Protocol::FILE_END: {
        QString id; ds >> id;
        if (ds.status() != QDataStream::Ok) break;
        auto it = m_inFiles.find(id);
        if (it == m_inFiles.end()) break;
        InFile* in = it.value();
        QString savedPath = in->savePath;
        if (in->file.isOpen()) in->file.close();
        bool complete = (in->received == in->size);
        if (!complete) {
            sendFileReject(id, "incomplete file");
            emit fileError(id, "incomplete file");
        } else {
            emit fileFinished(id, savedPath, false);
        }
        delete in;
        m_inFiles.erase(it);
        break;
    }
    default:
        break;
    }
}

void PeerConnection::sendText(const QString& message) {
    if (!isConnected()) {
        emit errorOccurred("not connected");
        return;
    }
    QByteArray payload = packDataStream([&](QDataStream& ds){ ds << message; });
    writeFrame(Protocol::TEXT, payload);
}

void PeerConnection::sendFile(const QString& filePath) {
    if (!isConnected()) {
        emit errorOccurred("not connected");
        return;
    }
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isReadable()) {
        emit errorOccurred("file not readable: " + filePath);
        return;
    }
    OutFile* f = new OutFile;
    f->transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f->path = filePath;
    f->name = fi.fileName();
    f->size = fi.size();
    m_outFiles.insert(f->transferId, f);

    QByteArray payload = packDataStream([&](QDataStream& ds){
        ds << f->transferId << f->name << f->size;
    });
    writeFrame(Protocol::FILE_OFFER, payload);
}

void PeerConnection::respondFileOffer(const QString& transferId, bool accept, const QString& savePath) {
    auto it = m_inFiles.find(transferId);
    if (it == m_inFiles.end()) return;
    InFile* in = it.value();

    if (!accept) {
        sendFileReject(transferId, "declined");
        delete in;
        m_inFiles.erase(it);
        return;
    }

    in->savePath = savePath;
    in->file.setFileName(savePath);
    if (!in->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit fileError(transferId, "cannot open save path: " + in->file.errorString());
        sendFileReject(transferId, "cannot open save path");
        delete in;
        m_inFiles.erase(it);
        return;
    }
    QByteArray payload = packDataStream([&](QDataStream& ds){ ds << transferId; });
    writeFrame(Protocol::FILE_ACCEPT, payload);
}

void PeerConnection::startNextOutgoing() {
    if (!m_currentSendingId.isEmpty()) return;
    while (!m_sendQueue.isEmpty()) {
        QString id = m_sendQueue.takeFirst();
        auto it = m_outFiles.find(id);
        if (it == m_outFiles.end()) continue;
        OutFile* f = it.value();
        f->file.setFileName(f->path);
        if (!f->file.open(QIODevice::ReadOnly)) {
            failOutgoingTransfer(id, "cannot open: " + f->file.errorString());
            continue;
        }
        f->sending = true;
        m_currentSendingId = id;
        continueSending();
        return;
    }
}

void PeerConnection::continueSending() {
    if (m_currentSendingId.isEmpty() || !m_socket) return;
    auto it = m_outFiles.find(m_currentSendingId);
    if (it == m_outFiles.end()) { m_currentSendingId.clear(); return; }
    OutFile* f = it.value();

    while (f->sent < f->size && m_socket->bytesToWrite() < WRITE_BUFFER_LIMIT) {
        QByteArray chunk = f->file.read(CHUNK_SIZE);
        if (chunk.isEmpty()) {
            QString reason = f->file.error() == QFileDevice::NoError
                ? QString("file ended before expected size")
                : QString("file read failed: %1").arg(f->file.errorString());
            failOutgoingTransfer(f->transferId, reason);
            return;
        }
        QByteArray payload = packDataStream([&](QDataStream& ds){
            ds << f->transferId << chunk;
        });
        writeFrame(Protocol::FILE_CHUNK, payload);
        f->sent += chunk.size();
        emit fileProgress(f->transferId, f->sent, f->size, true);
    }

    if (f->sent >= f->size) {
        QByteArray payload = packDataStream([&](QDataStream& ds){ ds << f->transferId; });
        writeFrame(Protocol::FILE_END, payload);
        f->file.close();
        emit fileFinished(f->transferId, f->path, true);
        delete f;
        m_outFiles.erase(it);
        m_currentSendingId.clear();
        QTimer::singleShot(0, this, &PeerConnection::startNextOutgoing);
    }
}

void PeerConnection::onBytesWritten(qint64) {
    if (!m_currentSendingId.isEmpty()) continueSending();
}

void PeerConnection::sendFileReject(const QString& transferId, const QString& reason) {
    if (transferId.isEmpty()) return;
    QByteArray payload = packDataStream([&](QDataStream& ds){
        ds << transferId << reason;
    });
    writeFrame(Protocol::FILE_REJECT, payload);
}

void PeerConnection::failOutgoingTransfer(const QString& transferId, const QString& reason) {
    auto it = m_outFiles.find(transferId);
    if (it == m_outFiles.end()) return;

    OutFile* f = it.value();
    if (f->file.isOpen()) f->file.close();
    emit fileError(transferId, reason);

    delete f;
    m_outFiles.erase(it);
    m_sendQueue.removeAll(transferId);

    if (m_currentSendingId == transferId) {
        m_currentSendingId.clear();
        QTimer::singleShot(0, this, &PeerConnection::startNextOutgoing);
    }
}

void PeerConnection::failIncomingTransfer(const QString& transferId, const QString& reason, bool notifyPeer) {
    auto it = m_inFiles.find(transferId);
    if (it == m_inFiles.end()) return;

    InFile* in = it.value();
    if (in->file.isOpen()) in->file.close();
    if (notifyPeer) sendFileReject(transferId, reason);
    emit fileError(transferId, reason);

    delete in;
    m_inFiles.erase(it);
}

void PeerConnection::cleanupTransfers(const QString& reason) {
    const QStringList outgoingIds = m_outFiles.keys();
    for (const QString& id : outgoingIds) {
        auto it = m_outFiles.find(id);
        if (it == m_outFiles.end()) continue;
        OutFile* f = it.value();
        if (f->file.isOpen()) f->file.close();
        emit fileError(id, reason);
        delete f;
    }
    m_outFiles.clear();
    m_sendQueue.clear();
    m_currentSendingId.clear();

    const QStringList incomingIds = m_inFiles.keys();
    for (const QString& id : incomingIds) {
        auto it = m_inFiles.find(id);
        if (it == m_inFiles.end()) continue;
        InFile* in = it.value();
        if (in->file.isOpen()) in->file.close();
        emit fileError(id, reason);
        delete in;
    }
    m_inFiles.clear();
}
