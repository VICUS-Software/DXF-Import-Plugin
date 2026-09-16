@echo off

:: Download and install GDAL DLLs for the DXF import plugin
:: Run this script from build\cmake directory

setlocal

:: Get script directory
set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..\..

:: Create temp directory for download
set TEMP_DIR=%SCRIPT_DIR%temp_gdal
if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"

:: Download gdal.7z if not already present
if not exist "%TEMP_DIR%\gdal.7z" (
    echo Downloading GDAL...
    curl https://deployment.vicus-software.com/gdal.7z -s --output "%TEMP_DIR%\gdal.7z"
    if ERRORLEVEL 1 goto fail
) else (
    echo gdal.7z already downloaded
)

:: Extract to temp directory
echo Extracting GDAL...
pushd "%TEMP_DIR%"
7z x gdal.7z -y > nul
if ERRORLEVEL 1 goto fail_pop
popd

:: Create target directory if it doesn't exist
if not exist "%PROJECT_DIR%\externals\gdal\lib" mkdir "%PROJECT_DIR%\externals\gdal\lib"

:: Install into every bin directory the build scripts write the plugin to - build.bat uses
:: bin\release, build_VC_x64.bat uses bin\release_x64 and the qmake builds use the debug ones.
:: Windows resolves the GDAL imports from the directory of the running executable, so each of
:: them needs its own copy - in an installation the DLLs sit next to the host executable.
for %%D in (debug debug_x64 release release_x64) do (
    call :install "%PROJECT_DIR%\bin\%%D"
    if ERRORLEVEL 1 goto fail
)

:: PROJ needs its proj.db at runtime, without it every EPSG lookup fails and georeferencing is dead.
:: The archive does not carry it yet - it is copied along as soon as it does.
if not exist "%TEMP_DIR%\proj\proj.db" (
    echo.
    echo WARNING: gdal.7z contains no proj\proj.db.
    echo          PROJ cannot resolve any EPSG code without it, so georeferenced DXF import
    echo          will not work. The archive on the deployment server has to be extended.
    echo.
)

:: Copy import library to externals
echo Copying gdal_i.lib to externals\gdal\lib...
copy /Y "%TEMP_DIR%\lib\gdal_i.lib" "%PROJECT_DIR%\externals\gdal\lib\" > nul
if ERRORLEVEL 1 goto fail

:: Clean up
echo Cleaning up...
REM rmdir /s /q "%TEMP_DIR%"

echo.
echo Done! GDAL files installed to:
echo   - bin\debug, bin\debug_x64, bin\release, bin\release_x64 (DLLs, proj, gdal-data)
echo   - externals\gdal\lib (gdal_i.lib)
exit /b 0

:: Installs the DLLs and the runtime data GDAL and PROJ need into the directory given as %1.
:install
if not exist "%~1" mkdir "%~1"
echo Copying GDAL DLLs to %~1...
xcopy /Y /Q "%TEMP_DIR%\*.dll" "%~1\" > nul
if ERRORLEVEL 1 exit /b 1
if exist "%TEMP_DIR%\proj\proj.db" (
    echo Copying PROJ database to %~1\proj...
    xcopy /Y /Q /E /I "%TEMP_DIR%\proj" "%~1\proj\" > nul
    if ERRORLEVEL 1 exit /b 1
)
if exist "%TEMP_DIR%\gdal-data" (
    echo Copying gdal-data to %~1\gdal-data...
    xcopy /Y /Q /E /I "%TEMP_DIR%\gdal-data" "%~1\gdal-data\" > nul
    if ERRORLEVEL 1 exit /b 1
)
exit /b 0

:fail_pop
popd
:fail
echo.
echo Failed!
exit /b 1
