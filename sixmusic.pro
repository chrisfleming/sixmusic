QT += core gui qml quick quickcontrols2 dbus network multimedia

CONFIG += c++17 release
TARGET = sixmusic
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/lastfm.h \
    src/mpris.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/lastfm.cpp \
    src/mpris.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
