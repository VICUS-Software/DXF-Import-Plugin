#!/bin/bash


# Build script for building application and all dependend libraries

# Command line options:
#   [reldeb|release|debug]		build type
#   [2 [1..n]]					cpu count
#   [verbose]					enable cmake to call verbose makefiles
#   []

CMAKELISTSDIR=$(pwd)
BUILDDIR="bb"

# set defaults
CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
MAKE_CPUCOUNT="8"
BUILD_DIR_SUFFIX="gcc"
COMPILER=""
SKIP_TESTS="false"
DISABLE_GUI=0

# parse parameters, except gprof and threadchecker
for var in "$@"
do

    if [[ $var = *[[:digit:]]* ]];
    then
		MAKE_CPUCOUNT=$var
		echo "Using $MAKE_CPUCOUNT CPUs for compilation"
    fi

    if [[ $var = "debug"  ]];
    then
		CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=Debug"
		echo "Debug build..."
    fi

    if [[ $var = "release"  ]];
    then
		CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=Release"
		echo "Release build..."
    fi

    if [[ $var = "reldeb"  ]];
    then
		CMAKE_BUILD_TYPE=" -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
		echo "RelWithDebInfo build..."
    fi

    if [[ $var = "gcc"  && $COMPILER = "" ]];
    then
		COMPILER="gcc"
		BUILD_DIR_SUFFIX="gcc"
		echo "GCC compiler build..."
		CMAKE_COMPILER_OPTIONS=""
	  fi

    if [[ $var = "verbose"  ]];
  	then
		CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON"
	  fi

done

# The plugin is loaded into the SIM-VICUS process, so it must be built against the very same Qt.
# SIM-VICUS uses the aqt install in ~/Qt/<version>/gcc_64 (see its build/cmake/install-qt-6.9.3.sh).
# Override AQT_QT_VERSION / AQT_QT_PREFIX in the environment to point at a different install.
AQT_QT_VERSION="${AQT_QT_VERSION:-6.9.3}"
AQT_QT_PREFIX="${AQT_QT_PREFIX:-$HOME/Qt/${AQT_QT_VERSION}/gcc_64}"

if [ -d "$AQT_QT_PREFIX" ]; then
	echo "Using aqt Qt at $AQT_QT_PREFIX"
	export PATH="$AQT_QT_PREFIX/bin:$PATH"
	export CMAKE_PREFIX_PATH="$AQT_QT_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
	export LD_LIBRARY_PATH="$AQT_QT_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
	export QT_PLUGIN_PATH="$AQT_QT_PREFIX/plugins"
	# bake aqt-Qt into the RPATH so the plugin does not pull in a second, system Qt at load time
	CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_BUILD_RPATH=$AQT_QT_PREFIX/lib -DCMAKE_INSTALL_RPATH=$AQT_QT_PREFIX/lib -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"
else
	echo "WARN: $AQT_QT_PREFIX not found, falling back to system Qt"
fi

# create build dir if not exists
BUILDDIR=$BUILDDIR-$BUILD_DIR_SUFFIX
if [ ! -d $BUILDDIR ]; then
    mkdir -p $BUILDDIR
fi

cd $BUILDDIR && cmake $CMAKE_OPTIONS $CMAKE_BUILD_TYPE $CMAKE_COMPILER_OPTIONS $CMAKELISTSDIR && make -j$MAKE_CPUCOUNT &&
cd $CMAKELISTSDIR &&
mkdir -p ../../bin/release &&
echo "*** Copying DXFImportPlugin to bin/release ***" &&
if [ -d $BUILDDIR/DXFImportPlugin/libDXFImportPlugin.so ]
then
	echo 'blub'
	# MacOS
	rm -rf ../../bin/release/libDXFImportPlugin.so
	cp -r $BUILDDIR/DXFImportPlugin/libDXFImportPlugin.so ../../bin/release/libDXFImportPlugin.so &&
    echo "All files copied successfully."
else
	if [ -e $BUILDDIR/DXFImportPlugin/libDXFImportPlugin.so ]
	then
		cp $BUILDDIR/DXFImportPlugin/libDXFImportPlugin.so ../../bin/release/libDXFImportPlugin.so
	fi
fi

