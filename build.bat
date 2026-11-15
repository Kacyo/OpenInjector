@echo off
setlocal

cd /d "%~dp0"

if /i "%~1"=="clean" (
    if exist build rmdir /s /q build
    if errorlevel 1 goto :fail
    echo [OpenInjector] Cleaned build directory.
    exit /b 0
)

where cmake >nul 2>nul
if errorlevel 1 goto :nocmake

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DOPENINJECTOR_BUILD_TESTS=ON
if errorlevel 1 goto :fail

cmake --build build --parallel
if errorlevel 1 goto :fail

echo.
echo [OpenInjector] Build succeeded.
echo   GUI: build\bin\OpenInjector.exe
echo   CLI: build\bin\OpenInjectorTool.exe
if exist build\bin\OpenInjector32.exe echo   x86 GUI: build\bin\OpenInjector32.exe
if exist build\bin\OpenInjectorTool32.exe echo   x86 CLI: build\bin\OpenInjectorTool32.exe
if exist build\bin\OpenInjectorHelper32.exe echo   x86 helper: build\bin\OpenInjectorHelper32.exe
exit /b 0

:nocmake
echo [OpenInjector] Error: cmake was not found in PATH.
echo Install CMake and a MinGW-w64 toolchain (g++, windres, mingw32-make).
exit /b 1

:fail
echo [OpenInjector] Build failed.
exit /b 1
