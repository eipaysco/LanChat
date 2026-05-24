QT       += core gui network widgets
CONFIG   += c++17

TARGET    = LanChat
TEMPLATE  = app

DEFINES  += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/ConnectDialog.cpp \
    src/SettingsDialog.cpp \
    src/PeerServer.cpp \
    src/PeerConnection.cpp \
    src/Discovery.cpp \
    src/Settings.cpp \
    src/DropArea.cpp

HEADERS += \
    src/MainWindow.h \
    src/ConnectDialog.h \
    src/SettingsDialog.h \
    src/PeerServer.h \
    src/PeerConnection.h \
    src/Discovery.h \
    src/Settings.h \
    src/DropArea.h \
    src/Protocol.h
