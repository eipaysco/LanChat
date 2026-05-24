#pragma once

#include <QFile>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

class PeerConnection : public QObject {
    Q_OBJECT
public:
    explicit PeerConnection(QObject* parent = nullptr);
    ~PeerConnection() override;

    // Server side: take an already-connected TCP socket.
    void adoptSocket(QTcpSocket* socket);

    // Client side: initiate TCP connect.
    void connectToPeer(const QString& host, quint16 port);

    void disconnectFromPeer();

    bool    isConnected() const;
    QString remoteName() const   { return m_remoteName; }
    QString remotePeerId() const { return m_remotePeerId; }
    QString remoteAddress() const;

    void sendText(const QString& message);
    void sendFile(const QString& filePath);
    void respondFileOffer(const QString& transferId, bool accept, const QString& savePath = QString());

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    void textReceived(const QString& message);

    void fileOffered  (const QString& transferId, const QString& fileName, qint64 fileSize);
    void fileProgress (const QString& transferId, qint64 bytes, qint64 total, bool sending);
    void fileFinished (const QString& transferId, const QString& savedPath, bool sending);
    void fileError    (const QString& transferId, const QString& reason);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onDisconnected();
    void onBytesWritten(qint64);
    void onHelloTimeout();

private:
    void setupSocket(QTcpSocket* s);
    void writeFrame(quint8 type, const QByteArray& payload);
    void processFrames();
    void handleFrame(quint8 type, const QByteArray& payload);

    void sendHello();

    void continueSending();
    void startNextOutgoing();
    void sendFileReject(const QString& transferId, const QString& reason);
    void failOutgoingTransfer(const QString& transferId, const QString& reason);
    void failIncomingTransfer(const QString& transferId, const QString& reason, bool notifyPeer);
    void cleanupTransfers(const QString& reason);

    struct OutFile {
        QString transferId;
        QString path;
        QString name;
        qint64  size = 0;
        QFile   file;
        qint64  sent = 0;
        bool    accepted = false;
        bool    sending  = false;
    };

    struct InFile {
        QString transferId;
        QString name;
        qint64  size = 0;
        QString savePath;
        QFile   file;
        qint64  received = 0;
    };

    QTcpSocket* m_socket = nullptr;
    QByteArray  m_buffer;

    bool    m_helloSent        = false;
    bool    m_helloReceived    = false;
    QString m_remotePeerId;
    QString m_remoteName;
    bool    m_emittedConnected = false;

    QTimer* m_helloTimer = nullptr;

    QMap<QString, OutFile*> m_outFiles;
    QList<QString>          m_sendQueue;
    QString                 m_currentSendingId;

    QMap<QString, InFile*>  m_inFiles;
};
