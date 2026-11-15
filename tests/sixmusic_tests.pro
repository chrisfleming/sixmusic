QT += core gui quick quickcontrols2 testlib dbus network multimedia
CONFIG += c++17 testcase
TARGET = sixmusic_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/theme.h ../src/translator.h
SOURCES += sixmusic_tests.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp
