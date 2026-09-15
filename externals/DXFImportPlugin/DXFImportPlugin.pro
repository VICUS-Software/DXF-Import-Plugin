# Project file for DXFImportPlugin
#
# remember to set DYLD_FALLBACK_LIBRARY_PATH on MacOSX
# set LD_LIBRARY_PATH on Linux

TARGET = DXFImportPlugin

QT += gui widgets

TEMPLATE = lib
CONFIG += plugin
CONFIG += static_and_dynamic
CONFIG += c++17

# Uncomment to disable deprecated Qt APIs before Qt 6
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

INCLUDEPATH += \
	src \
	externals/libdxfrw/src \
	externals/libdxfrw/src/intern \
	externals/IBK/src \
	externals/IBKMK/src \
	externals/QtExt/src \
	externals/glm/src \
	externals/glm/src/glm \
	externals/glm/src/gtx \
	externals/TiCPP/src

# -------------------------------------------------
# Build configuration
# -------------------------------------------------

CONFIG(debug, debug|release) {
	BUILD_TYPE = debug
} else {
	BUILD_TYPE = release
}

OBJECTS_DIR = $$BUILD_TYPE
MOC_DIR     = moc
UI_DIR      = ui

# Output directories
win32 {
	DESTDIR    = ../lib
	DLLDESTDIR = bin/$$BUILD_TYPE
} else {
	DESTDIR    = bin/$$BUILD_TYPE
}

# -------------------------------------------------
# Sources / Headers
# -------------------------------------------------

SOURCES += \
	src/Constants.cpp \
	src/DXFImportPlugin.cpp \
	src/Drawing.cpp \
	src/Georeferencing.cpp \
	src/DrawingLayer.cpp \
	src/ImportDXFDialog.cpp \
	src/Object.cpp \
	src/Utilities.cpp

HEADERS += \
	src/Constants.h \
	src/Drawing.h \
	src/DrawingLayer.h \
	src/Georeferencing.h \
	src/ImportDXFDialog.h \
	src/Object.h \
	src/RotationMatrix.h \
	src/SVCommonPluginInterface.h \
	src/SVImportPluginInterface.h \
	src/DXFImportPlugin.h \
	src/Utilities.h

FORMS += \
	src/ImportDXFDialog.ui

TRANSLATIONS +=

CODECFORSRC = UTF-8

# -------------------------------------------------
# Libraries
# -------------------------------------------------

LIB_DIR = externals/lib

QMAKE_LIBDIR += $$LIB_DIR
LIBS += -L$$LIB_DIR

LIBS += \
	-lTiCPP \
	-lIBKMK \
	-llibdxfrw \
	-lQtExt \
	-lglm \
	-lIBK

win32:LIBS += -liphlpapi -lshell32

# GDAL support, needed for the coordinate reference systems of georeferenced drawings
linux|macx {
	CONFIG += link_pkgconfig
	PKGCONFIG += gdal
}

win32 {
	# precompiled binaries, run build/cmake/download_gdal.bat to fetch them
	!exists($$PWD/../gdal/lib/gdal_i.lib) {
		error("GDAL not found in externals/gdal/lib. Run build/cmake/download_gdal.bat!")
	}
	INCLUDEPATH += $$PWD/../gdal/include
	LIBS += -L$$PWD/../gdal/lib -lgdal_i
}

win32-msvc* {
	QMAKE_CXXFLAGS += /std:c++17
}
