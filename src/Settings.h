#pragma once

#include <QString>
#include <QtGlobal>

class Settings {
public:
    static Settings& instance();
    static void setProfileName(const QString& name);
    static QString profileName();

    // Network
    quint16 tcpPort() const;
    void    setTcpPort(quint16 port);

    quint16 udpPort() const;
    void    setUdpPort(quint16 port);

    int     broadcastIntervalMs() const;
    void    setBroadcastIntervalMs(int ms);

    // Identity
    QString machineName() const;
    void    setMachineName(const QString& name);

    QString peerId() const; // persistent UUID, generated on first call

    // Files
    QString defaultSaveDir() const;
    void    setDefaultSaveDir(const QString& dir);

    // Paths
    QString dataDir() const;       // %APPDATA%/LanChat
    QString configPath() const;    // dataDir/config.ini

private:
    Settings();
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;
};
