#pragma once

#include <QtGlobal>

namespace Protocol {

// Frame types
constexpr quint8 HELLO       = 0x01;
constexpr quint8 TEXT        = 0x02;
constexpr quint8 FILE_OFFER  = 0x03;
constexpr quint8 FILE_ACCEPT = 0x04;
constexpr quint8 FILE_REJECT = 0x05;
constexpr quint8 FILE_CHUNK  = 0x06;
constexpr quint8 FILE_END    = 0x07;

// Sizing
constexpr int CHUNK_SIZE          = 64 * 1024;     // 64 KB per FILE_CHUNK payload
constexpr int WRITE_BUFFER_LIMIT  = 1024 * 1024;   // back-pressure threshold

// Defaults
constexpr quint16 DEFAULT_TCP_PORT             = 45454;
constexpr quint16 DEFAULT_UDP_PORT             = 45455;
constexpr int     DEFAULT_BROADCAST_INTERVAL_MS = 3000;
constexpr int     PEER_TIMEOUT_MS              = 10000;

// Discovery
constexpr const char* DISCOVERY_MAGIC = "LANCHAT-v1";
constexpr const char* APP_VERSION     = "1.0.0";

} // namespace Protocol
