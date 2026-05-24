#include "Settings.h"
#include "Protocol.h"

#include <QDir>
#include <QHostInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString& configuredProfileName() {
    static QString name;
    return name;
}

QString cleanProfileName(const QString& rawName) {
    QString cleaned;
    const QString trimmed = rawName.trimmed();
    cleaned.reserve(trimmed.size());

    for (const QChar& ch : trimmed) {
        if (ch.isLetterOrNumber() || ch == '_' || ch == '-') {
            cleaned.append(ch);
        } else if (!cleaned.endsWith('_')) {
            cleaned.append('_');
        }
    }

    while (cleaned.startsWith('_')) cleaned.remove(0, 1);
    while (cleaned.endsWith('_')) cleaned.chop(1);
    if (cleaned.isEmpty() && !trimmed.isEmpty()) cleaned = "profile";
    return cleaned;
}

QString configuredDataDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString profile = configuredProfileName();
    if (!profile.isEmpty()) dir += "/profiles/" + profile;
    return dir;
}

QSettings& store() {
    // Persistent INI file inside the data dir.
    static QString path = []{
        QString dir = configuredDataDir();
        QDir().mkpath(dir);
        return dir + "/config.ini";
    }();
    static QSettings s(path, QSettings::IniFormat);
    return s;
}

} // namespace

Settings& Settings::instance() {
    static Settings s;
    return s;
}

void Settings::setProfileName(const QString& name) {
    configuredProfileName() = cleanProfileName(name);
}

QString Settings::profileName() {
    return configuredProfileName();
}

Settings::Settings() {
    QDir().mkpath(dataDir());
}

QString Settings::dataDir() const {
    return configuredDataDir();
}

QString Settings::configPath() const { return dataDir() + "/config.ini"; }

quint16 Settings::tcpPort() const {
    return static_cast<quint16>(store().value("network/tcpPort", Protocol::DEFAULT_TCP_PORT).toUInt());
}
void Settings::setTcpPort(quint16 port) {
    store().setValue("network/tcpPort", port);
}

quint16 Settings::udpPort() const {
    return static_cast<quint16>(store().value("network/udpPort", Protocol::DEFAULT_UDP_PORT).toUInt());
}
void Settings::setUdpPort(quint16 port) {
    store().setValue("network/udpPort", port);
}

int Settings::broadcastIntervalMs() const {
    return store().value("network/broadcastIntervalMs", Protocol::DEFAULT_BROADCAST_INTERVAL_MS).toInt();
}
void Settings::setBroadcastIntervalMs(int ms) {
    store().setValue("network/broadcastIntervalMs", ms);
}

QString Settings::machineName() const {
    QString defName = QHostInfo::localHostName();
    if (defName.isEmpty()) defName = "LanChat";
    return store().value("identity/machineName", defName).toString();
}
void Settings::setMachineName(const QString& name) {
    store().setValue("identity/machineName", name);
}

QString Settings::peerId() const {
    QString id = store().value("identity/peerId").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        store().setValue("identity/peerId", id);
        store().sync();
    }
    return id;
}

QString Settings::defaultSaveDir() const {
    QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/LanChat";
    return store().value("files/defaultSaveDir", def).toString();
}
void Settings::setDefaultSaveDir(const QString& dir) {
    store().setValue("files/defaultSaveDir", dir);
}
