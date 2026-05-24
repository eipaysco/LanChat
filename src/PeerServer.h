#pragma once

#include <QTcpServer>

class QTcpSocket;

class PeerServer : public QTcpServer {
    Q_OBJECT
public:
    explicit PeerServer(QObject* parent = nullptr);

    bool start(quint16 port);
    void stop();

signals:
    // Fires when a new peer's TCP socket is accepted. Caller takes ownership.
    void newPeerConnection(QTcpSocket* socket);
    void listenError(const QString& message);

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};
