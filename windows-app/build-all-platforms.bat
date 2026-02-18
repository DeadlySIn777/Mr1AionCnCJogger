@echo off
rem AIONMECH Pendant Controller - Universal Platform Build Script
rem Builds for ALL Windows architectures: x64, x86 (32-bit)

echo ========================================
echo   AIONMECH Universal Build Script
echo ========================================
echo.
echo Building for ALL Windows platforms:
echo - x64 (64-bit Windows 10/11)
echo - ia32 (32-bit Windows 10/11)
echo.

rem Check if Node.js and npm are installed
where npm >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: npm not found. Please install Node.js first.
    pause
    exit /b 1
)

rem Install dependencies
echo Installing dependencies...
call npm install
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to install dependencies
    pause
    exit /b 1
)

rem Clean previous builds
echo Cleaning previous builds...
if exist dist rmdir /s /q dist
if exist node_modules\.cache\electron rmdir /s /q node_modules\.cache\electron

rem Build for ALL architectures
echo.
echo ========================================
echo Building UNIVERSAL version (All platforms)...
echo ========================================

rem Use package-universal.json for universal build
copy /y package-universal.json package.json

rem Build all Windows targets
echo Building Windows x64 + ia32 (both 32-bit and 64-bit)...
call npm run build-universal
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo           BUILD COMPLETE!
echo ========================================
echo.
echo Built files are in the 'dist' folder:
echo.
dir /b dist
echo.
echo Compatible with:
echo - Windows 10/11 (all editions)
echo - Intel/AMD processors (any generation)
echo - Integrated graphics (Intel HD, AMD)
echo - Dedicated graphics (NVIDIA, AMD)
echo - Mini PCs, laptops, desktops
echo - 2GB+ RAM systems
echo.
pause