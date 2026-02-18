@echo off
rem AIONMECH Pendant Controller - Portable Build for Maximum Compatibility
rem Creates portable executables that run on ANY Windows 10/11 system

echo ========================================
echo   AIONMECH Portable Build Script
echo ========================================
echo.
echo Building PORTABLE versions for universal compatibility:
echo - No installation required
echo - Runs from any folder/USB drive
echo - Works on locked/restricted systems
echo - Compatible with ALL Windows 10/11 PCs
echo.

rem Install dependencies
echo Installing dependencies...
call npm install
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to install dependencies
    pause
    exit /b 1
)

rem Use universal package configuration
copy /y package-universal.json package.json

rem Clean previous builds
if exist dist rmdir /s /q dist

rem Build portable versions
echo.
echo Building portable executables...
call npm run build-portable
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Portable build failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo      PORTABLE BUILD COMPLETE!
echo ========================================
echo.
echo Created portable executables:
dir /b dist\*.exe
echo.
echo These files run on ANY Windows PC:
echo - Download and double-click to run
echo - No admin rights needed
echo - No installation required
echo - Works with any CPU/GPU combination
echo - Compatible with 32-bit and 64-bit systems
echo.
pause