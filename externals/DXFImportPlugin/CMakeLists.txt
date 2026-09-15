# CMakeLists.txt file for DXFImportPlugin library

project(DXFImportPlugin)

# GDAL support (coordinate reference systems for georeferencing)
if(WIN32)
	# Windows: use precompiled binaries, run build/cmake/download_gdal.bat to fetch them
	include_directories(${PROJECT_SOURCE_DIR}/../gdal/include)
	link_directories(${PROJECT_SOURCE_DIR}/../gdal/lib)
	set(GDAL_LIBRARY gdal_i)
else()
	# Linux/macOS: find system GDAL (install with: sudo apt install libgdal-dev)
	find_package(GDAL REQUIRED)
	include_directories(${GDAL_INCLUDE_DIRS})
	set(GDAL_LIBRARY ${GDAL_LIBRARIES})
endif()

# Add include directories
include_directories(	
	${PROJECT_SOURCE_DIR}/src			# Needed so that ui-generated header files find our own headers
	${PROJECT_SOURCE_DIR}/../IBK/src
	${PROJECT_SOURCE_DIR}/../IBKMK/src
	${PROJECT_SOURCE_DIR}/../QtExt/src
	${PROJECT_SOURCE_DIR}/../TiCPP/src
	${PROJECT_SOURCE_DIR}/../glm/src
	${PROJECT_SOURCE_DIR}/../glm/src/glm 
	${PROJECT_SOURCE_DIR}/../glm/src/gtx
	${PROJECT_SOURCE_DIR}/../TiCPP/src
	${PROJECT_SOURCE_DIR}/../libdxfrw/src
	${PROJECT_SOURCE_DIR}/../libdxfrw/src/intern
	${Qt6Widgets_INCLUDE_DIRS}
)

# Collect a list of all source files of the DXFImportPlugin library
file(GLOB LIB_SRCS ${PROJECT_SOURCE_DIR}/src/*.cpp)

# Collect a list of all header files of the DXFImportPlugin library
file(GLOB LIB_HDRS ${PROJECT_SOURCE_DIR}/src/*.h)

# Collect a list of all form files of the DXFImportPlugin library
file(GLOB LIB_UIS ${PROJECT_SOURCE_DIR}/src/*.ui)
    
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj ")
endif()

# Run uic and moc compilers
qt6_add_resources(LIB_RC_SRCS ${LIB_QRCS})
qt6_wrap_ui(LIB_UI_SRCS ${LIB_UIS})
qt6_wrap_cpp(LIB_MOC_SRCS ${LIB_HDRS})

# Build the library
add_library(DXFImportPlugin SHARED
	${LIB_SRCS}
	${LIB_HDRS}
	${LIB_MOC_SRCS}
	${LIB_UI_SRCS}
	${LIB_RC_SRCS}
)

# Option to build using shared libraries (if needed)
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

# generate list of all required QT_LIBRARIES
set ( QT_LIBRARIES
	Qt6::Widgets
	Qt6::PrintSupport
	Qt6::Svg
	Qt6::Xml
	Qt6::Network
	Qt6::Concurrent
)

# Create a list for the libraries
set(LIBRARIES_TO_LINK "")
list(APPEND LIBRARIES_TO_LINK
		IBK
		IBKMK
		TiCPP
		libdxfrw
		QtExt
		glm
		${QT_LIBRARIES}
		${GDAL_LIBRARY}
)

# Specify the static libraries to link with
target_link_libraries(DXFImportPlugin ${LIBRARIES_TO_LINK})

# Ensure position-independent code (PIC) is enabled for the static libraries
set_target_properties(DXFImportPlugin PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ensure the output is a DLL
set_target_properties(DXFImportPlugin PROPERTIES OUTPUT_NAME "DXFImportPlugin")
