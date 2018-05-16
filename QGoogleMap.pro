TARGET  = QGoogleMap.exe
SOURCES += QGoogleMap.cpp
HEADERS += QGoogleMap.h
RESOURCES += QGoogleMap.qrc

CONFIG += qt
Qt += core
Qt += gui
QT += network
QT += xml

LIBS += -lz

QMAKE_CXXFLAGS += -g -ggdb
QMAKE_CXXFLAGS += -std=c++11

OBJECTS_DIR = build/
MOC_DIR     = build/
RCC_DIR     = build/
