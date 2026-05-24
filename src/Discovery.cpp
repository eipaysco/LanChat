#include "Discovery.h"
#include "Protocol.h"
#include "Settings.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

Discovery::Discovery(QObject* parent) : QObject(parent) {
    m_socket    = new QUdpSocket(this);
    m_broadcast = new QTimer(this);
    m_sweep     = new QTimer(this);

    connect(m_socket,    &QUdpSocket::readyRead, this, &Discovery::onReadyRead);
    connect(m_broadcast, &QTimer::timeout,       this, &Discovery::onBroadcastTick);
    connect(m_sweep,     &QTimer::timeout,       this, &Discovery::onSweepTick);
}

Discovery::~Discovery() = default;

bool Discovery::start() {
    quint16 port = Settings::instance().udpPort();
    bool ok = m_socket->bind(QHostAddress::AnyIPv4, port,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (!ok) return false;

    int interval = Settings::instance().broadcastIntervalMs();
    m_broadcast->start(interval);
    m_sweep->start(1000);

    // Announce immediately so we appear in peers' lists fast.
    broadcastOnce(false);
    return true;
}

void Discovery::stop() {
    m_broadcast->stop();
    m_sweep->stop();
    m_socket->close();
}

void Discovery::sayBye() {
    broadcastOnce(true);
}

QList<DiscoveredPeer> Discovery::peers() const {
    return m_peers.values();
}

QByteArray Discovery::buildAnnouncement(bool bye) const {
    QJsonObject o;
    o.insert("magic",       QString::fromLatin1(Protocol::DISCOVERY_MAGIC));
    o.insert("peerId",      Settings::instance().peerId());
    o.insert("machineName", Settings::instance().machineName());
    o.insert("tcpPort",     Settings::instance().tcpPort());
    o.insert("timestamp",   double(QDateTime::currentSecsSinceEpoch()));
    if (bye) o.insert("bye", true);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void Discovery::broadcastOnce(bool bye) {
    QByteArray payload = buildAnnouncement(bye);
    quint16 port = Settings::instance().udpPort();

    // Per-interface subnet broadcast (Windows-friendly).
    int sent = 0;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))      continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)   continue;
        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            if (e.broadcast().isNull()) continue;
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            m_socket->writeDatagram(payload, e.broadcast(), port);
            ++sent;
        }
    }
    // Fallback global broadcast.
    m_socket->writeDatagram(payload, QHostAddress::Broadcast, port);
    Q_UNUSED(sent)
}

void Discovery::onBroadcastTick() {
    broadcastOnce(false);
}

void Discovery::onReadyRead() {
    QHostAddress src;
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(int(m_socket->pendingDatagramSize()));
        quint16 srcPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &src, &srcPort);

        QJsonParseError pe{};
        QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) continue;
        QJsonObject o = doc.object();
        if (o.value("magic").toString() != QString::fromLatin1(Protocol::DISCOVERY_MAGIC)) continue;

        QString peerId = o.value("peerId").toString();
        if (peerId.isEmpty() || peerId == Settings::instance().peerId()) continue;

        if (o.value("bye").toBool(false)) {
            if (m_peers.remove(peerId) > 0) emit peerLost(peerId);
            continue;
        }

        DiscoveredPeer p;
        p.peerId      = peerId;
        p.machineName = o.value("machineName").toString();
        p.tcpPort     = quint16(o.value("tcpPort").toInt(Protocol::DEFAULT_TCP_PORT));
        p.address     = src;
        p.lastSeenMs  = QDateTime::currentMSecsSinceEpoch();

        m_peers.insert(peerId, p);
        emit peerSeen(p);
    }
}

void Discovery::onSweepTick() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList expired;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (now - it.value().lastSeenMs > Protocol::PEER_TIMEOUT_MS)
            expired << it.key();
    }
    for (const QString& id : expired) {
        m_peers.remove(id);
        emit peerLost(id);
    }
}
