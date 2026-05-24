#include "PeerServer.h"

#include <QTcpSocket>

PeerServer::PeerServer(QObject* parent) : QTcpServer(parent) {}

bool PeerServer::start(quint16 port) {
    if (isListening()) close();
    if (!listen(QHostAddress::Any, port)) {
        emit listenError(errorString());
        return false;
    }
    return true;
}

void PeerServer::stop() {
    close();
}

void PeerServer::incomingConnection(qintptr socketDescriptor) {
    auto* s = new QTcpSocket(this);
    if (!s->setSocketDescriptor(socketDescriptor)) {
        s->deleteLater();
        return;
    }
    emit newPeerConnection(s);
}
