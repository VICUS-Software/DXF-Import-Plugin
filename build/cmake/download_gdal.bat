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

:: Copy DLLs to target directories
if not exist "%PROJECT_DIR%\bin\debug" mkdir "%PROJECT_DIR%\bin\debug"
if not exist "%PROJECT_DIR%\bin\release_x64" mkdir "%PROJECT_DIR%\bin\release_x64"

echo Copying GDAL DLLs to bin\debug...
xcopy /Y /Q "%TEMP_DIR%\*.dll" "%PROJECT_DIR%\bin\debug\" > nul
if ERRORLEVEL 1 goto fail

echo Copying GDAL DLLs to bin\release_x64...
xcopy /Y /Q "%TEMP_DIR%\*.dll" "%PROJECT_DIR%\bin\release_x64\" > nul
if ERRORLEVEL 1 goto fail

:: Copy import library to externals
echo Copying gdal_i.lib to externals\gdal\lib...
copy /Y "%TEMP_DIR%\lib\gdal_i.lib" "%PROJECT_DIR%\externals\gdal\lib\" > nul
if ERRORLEVEL 1 goto fail

:: Clean up
echo Cleaning up...
REM rmdir /s /q "%TEMP_DIR%"

echo.
echo Done! GDAL files installed to:
echo   - bin\debug (DLLs)
echo   - bin\release_x64 (DLLs)
echo   - externals\gdal\lib (gdal_i.lib)
exit /b 0

:fail_pop
popd
:fail
echo.
echo Failed!
exit /b 1
