# Project file for DXFTEstBed
#
# remember to set DYLD_FALLBACK_LIBRARY_PATH on MacOSX
# set LD_LIBRARY_PATH on Linux

TARGET = DXFTEstBed
TEMPLATE = app

# this pri must be sourced from all our libraries,
# it contains all functions defined for casual libraries
#include( ../externals/IBK/projects/Qt/IBK.pri )

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11 static

greaterThan(QT_MAJOR_VERSION, 4) {
contains(QT_ARCH, i386): {
DIR_PREFIX =
} else {
DIR_PREFIX = _x64
}
} else {
DIR_PREFIX =
}

CONFIG(debug, debug|release) {
OBJECTS_DIR = debug$${DIR_PREFIX}
DESTDIR = ../bin/debug$${DIR_PREFIX}
}
else {
OBJECTS_DIR = release$${DIR_PREFIX}
DESTDIR = ../bin/release$${DIR_PREFIX}
}

MOC_DIR = moc
UI_DIR = ui

win32-msvc* {
QMAKE_CXXFLAGS += /wd4996
QMAKE_CFLAGS += /wd4996
DEFINES += _CRT_SECURE_NO_WARNINGS
DEFINES += NOMINMAX
}
else {
QMAKE_CXXFLAGS += -std=c++11
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

LIBS += -L../externals/lib_x64

LIBS += \
-lTiCPP \
-lIBKMK \
-lIBK \
-lglm \
-lDXFImportPlugin

# GDAL, pulled in through the plugin's Georeferencing class
linux|macx {
	CONFIG += link_pkgconfig
	PKGCONFIG += gdal
}

win32 {
	INCLUDEPATH += ../externals/gdal/include
	LIBS += -L../externals/gdal/lib -lgdal_i
}

INCLUDEPATH = \
src \
../externals/DXFImportPlugin/src \
../externals/TiCPP/src \
../externals/IBKMK/src \
../externals/IBK/src \
../externals/libdxfrw/src \
../externals/libdxfrw/src/intern \
../externals/glm/src \
../externals/glm/src\glm \
../externals/glm/src\gtx \
../externals/QtExt/src



DEPENDPATH = $${INCLUDEPATH}

SOURCES += \
src/main.cpp \
src/mainwindow.cpp

HEADERS += \
src/mainwindow.h

FORMS += \
src/mainwindow.ui

#TRANSLATIONS += resources/translations/IFC2BESTest_de.ts
CODECFORSRC = UTF-8

