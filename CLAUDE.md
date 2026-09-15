# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DXFImportPlugin is a Qt-based plugin for importing DXF (AutoCAD Drawing Exchange Format) files into SIM-VICUS, a building and district energy simulation tool. It uses the Qt Plugin System (`Q_PLUGIN_METADATA` / `SVImportPluginInterface`). Current version: 1.3.0.

## Build Commands

### Linux (CMake)
```bash
cd build/cmake
./build.sh                  # RelWithDebInfo (default), 8 CPUs
./build.sh release 4        # Release build, 4 CPUs
./build.sh debug            # Debug build
```
The build output (shared library `libDXFImportPlugin.so`) is copied to `bin/release/`.

### Windows (VC 2022 + Qt6)
```bat
cd build\cmake
build.bat
```
Requires Visual Studio 2022, Qt 6.9.3, and vcpkg with zlib. Uses `jom` for parallel builds.

### CMake manually
```bash
cd build/cmake
mkdir -p bb-gcc && cd bb-gcc
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j$(nproc)
```

## Architecture

### Plugin Interface Chain
`SVCommonPluginInterface` → `SVImportPluginInterface` → `DXFImportPlugin`

The host app (SIM-VICUS) loads this plugin at runtime, calls `setLanguage()` for i18n, registers the menu via `importMenuCaption()`, and triggers `import()` when the user selects DXF import.

### Data Flow
0. **Project context**: SIM-VICUS hands the world coordinate origin of the open project into
   `import()` via `projectText` (the plugin declares `"accepts-project-context"` in `metadata.json`).
   If the project has no origin, yet, a georeferenced drawing proposes one and writes it back as
   `<WorldCoordinateOrigin>`.
1. **DXF Parsing**: `DRW_InterfaceImpl` (nested in `ImportDXFDialog`) implements libdxfrw's callback interface to receive DXF entities
2. **Data Model**: Entities are stored in `Drawing` — the central class managing all geometry (points, lines, polylines, circles, ellipses, arcs, solids, text, dimensions), blocks/inserts, layers, and transformation matrices
3. **Interactive Dialog**: `ImportDXFDialog` provides UI for scale detection, unit selection, origin positioning, and layer management
4. **Export**: `Drawing` serializes to VICUS-compatible XML via TiCPP, which is appended to the VICUS project

### Key Source Files (`externals/DXFImportPlugin/src/`)
- **`DXFImportPlugin.h/cpp`** — Plugin entry point, implements the interface
- **`Drawing.h/cpp`** (~3300 LOC) — Central data model with all entity types, transformation matrices, spatial field indexing (10m grid cells), triangulation, and XML serialization
- **`ImportDXFDialog.h/cpp`** (~1500 LOC) — Import dialog with `DRW_InterfaceImpl` for DXF parsing, auto scale detection from `$INSUNIT` header, weighted median center calculation
- **`DrawingLayer.h/cpp`** — DXF layer representation (color, line weight, visibility)
- **`Constants.h/cpp`** — Segment counts for arcs/circles (30), line weight defaults
- **`Utilities.h/cpp`** — Unique name generation, XML template helpers
- **`RotationMatrix.h`** — 3D rotation via QQuaternion + GLM
- **`Georeferencing.h/cpp`** — reads the coordinate reference system of a DXF (`.prj` sidecar or
  `AcDbGeoData` object) and computes the placement of the drawing in the UTM system of the project

### Vendored Dependencies (`externals/`)
| Library | Purpose |
|---------|---------|
| **libdxfrw** | DXF/DWG reader/writer (callback-based) |
| **IBK** | Core utilities (message handling, string conversion) |
| **IBKMK** | Math kernel (3D vectors, line/polygon operations) |
| **QtExt** | Qt extensions (custom widgets, language handling) |
| **TiCPP** | TinyXML C++ wrapper for VICUS XML I/O |
| **glm** | OpenGL Mathematics (matrix transforms) |
| **clipper** | Polygon clipping |

### External Dependencies

| Library | Purpose | How it is found |
|---------|---------|-----------------|
| **Qt 6.9.3** | GUI. The plugin is loaded into the SIM-VICUS process, so it must be built against the *same* Qt. `build.sh` picks up the aqt install in `~/Qt/6.9.3/gcc_64` (override with `AQT_QT_PREFIX` / `AQT_QT_VERSION`). |
| **GDAL** | Coordinate reference systems for georeferencing (OSR part only) | Linux/macOS: `find_package(GDAL)` / `PKGCONFIG += gdal`, install `libgdal-dev`. Windows: headers are vendored in `externals/gdal/include`, run `build/cmake/download_gdal.bat` for `gdal_i.lib`. |

Build dependency order: IBK, IBKMK, TiCPP, glm, libdxfrw, QtExt → DXFImportPlugin

### Required Qt5 Modules
Widgets, Xml, Svg, PrintSupport, Network, Concurrent

## Coding Conventions

- Member variables: `m_memberName`
- Constants: `UPPER_CASE` (e.g., `DEFAULT_LINE_WEIGHT`)
- Methods: camelCase
- C++11 standard, STL containers (`std::vector`, `std::map`, `std::set`)
- Error handling via `IBK::Exception` with descriptive messages
- Qt parent-child ownership model for UI objects
- Dirty flags (`m_dirtyLocalPoints`, `m_dirtyGlobalPoints`) for lazy re-computation of geometry

## Testing

No unit test framework. Testing is done via the `DXFTestBed/` application which loads the plugin and exercises the import workflow interactively.

## CI

GitHub Actions builds on Ubuntu 20.04/22.04/24.04 and Windows VC 2022. Workflows are in `.github/workflows/`.
