TEMPLATE = lib
CONFIG += plugin c++17
QT += qml quick gui-private
TARGET = encomglobeplugin

SOURCES += src/globeitem.cpp src/globerenderer.cpp src/overlays.cpp src/plugin.cpp \
           src/particleglobeitem.cpp src/particlerenderer.cpp \
           src/overlayscene.cpp src/overlayrenderer.cpp
HEADERS += src/globeitem.h src/globerenderer.h src/overlays.h \
           src/particleglobeitem.h src/particlerenderer.h \
           src/overlayscene.h src/overlayrenderer.h
RESOURCES += encomglobe.qrc
