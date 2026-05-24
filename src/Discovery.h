#pragma once

#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QString>

class QUdpSocket;
class QTimer;

struct DiscoveredPeer {
    QString      peerId;
    QString      machineName;
    QHostAddress address;
    quint16      tcpPort = 0;
    qint64       lastSeenMs = 0;
};

class Discovery : public QObject {
    Q_OBJECT
public:
    explicit Discovery(QObject* parent = nullptr);
    ~Discovery() override;

    bool start();
    void stop();

    // Send a final bye packet (best-effort, blocking write).
    void sayBye();

    QList<DiscoveredPeer> peers() const;

signals:
    void peerSeen(const DiscoveredPeer& peer);
    void peerLost(const QString& peerId);

private slots:
    void onReadyRead();
    void onBroadcastTick();
    void onSweepTick();

private:
    void broadcastOnce(bool bye = false);
    QByteArray buildAnnouncement(bool bye) const;

    QUdpSocket* m_socket    = nullptr;
    QTimer*     m_broadcast = nullptr;
    QTimer*     m_sweep     = nullptr;

    QMap<QString, DiscoveredPeer> m_peers;
};
