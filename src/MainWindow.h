#pragma once

#include <QMainWindow>
#include <QMap>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;

class Discovery;
class PeerServer;
class PeerConnection;
class DropArea;
struct DiscoveredPeer;

class QTcpSocket;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onManualConnect();
    void onOpenSettings();
    void onPeerDoubleClicked(QListWidgetItem* item);
    void onSendClicked();
    void onChooseFileClicked();
    void onDisconnectClicked();
    void onFilesDropped(const QStringList& paths);

    void onPeerSeen(const DiscoveredPeer& p);
    void onPeerLost(const QString& peerId);

    void onNewIncomingConnection(QTcpSocket* socket);

    void onConnConnected();
    void onConnDisconnected();
    void onConnError(const QString& msg);
    void onConnText(const QString& msg);
    void onConnFileOffered (const QString& id, const QString& name, qint64 size);
    void onConnFileProgress(const QString& id, qint64 bytes, qint64 total, bool sending);
    void onConnFileFinished(const QString& id, const QString& path, bool sending);
    void onConnFileError   (const QString& id, const QString& reason);

private:
    void buildUi();
    void startNetwork();
    void replaceConnection();
    void wireConnectionSignals();
    void appendLog(const QString& html);
    void setConnectedState(bool connected, const QString& remoteDesc = QString());
    QListWidgetItem* itemForTransfer(const QString& id);
    QListWidgetItem* createTransferItem(const QString& id, const QString& text);
    static QString humanSize(qint64 bytes);

    QListWidget* m_peerList       = nullptr;
    QTextEdit*   m_chat           = nullptr;
    QListWidget* m_transfers      = nullptr;
    QLineEdit*   m_input          = nullptr;
    QPushButton* m_sendBtn        = nullptr;
    QPushButton* m_chooseBtn      = nullptr;
    QPushButton* m_disconnectBtn  = nullptr;
    QLabel*      m_statusTop      = nullptr;
    QLabel*      m_statusBottom   = nullptr;
    DropArea*    m_dropArea       = nullptr;

    Discovery*      m_discovery = nullptr;
    PeerServer*     m_server    = nullptr;
    PeerConnection* m_conn      = nullptr;

    QMap<QString, QListWidgetItem*> m_peerItems;
    QMap<QString, QListWidgetItem*> m_transferItems;
};
