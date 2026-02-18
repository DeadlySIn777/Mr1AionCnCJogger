@echo off
echo ===================================================================
echo AIONMECH Pendant Controller - Build System
echo Supporting CNC machines: FireControl, CutControl
echo ===================================================================
echo.

REM Check if Node.js is installed
where node >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Node.js is not installed or not in PATH
    echo Please install Node.js from https://nodejs.org/
    pause
    exit /b 1
)

REM Check if we're in the right directory
if not exist "src\main-universal.js" (
    echo ERROR: main-universal.js not found
    echo Please run this script from the windows-app directory
    pause
    exit /b 1
)

echo Current directory: %CD%
echo Node.js version: 
node --version
echo NPM version:
npm --version
echo.

REM Update package.json for universal build
echo Updating package.json for universal build...
copy package-universal.json package.json >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo WARNING: Could not update package.json, using existing
)
echo.

REM Install dependencies if needed
echo Checking dependencies...
if not exist "node_modules" (
    echo Installing dependencies...
    npm install
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to install dependencies
        pause
        exit /b 1
    )
) else (
    echo Dependencies already installed, updating...
    npm update
)
echo.

REM Clean previous builds
echo Cleaning previous builds...
if exist "dist" rmdir /s /q "dist"
echo.

REM Build universal version
echo ===================================================================
echo Building AIONMECH Pendant Controller v2.0.0
echo ===================================================================
echo Target: Windows 10/11 (x64 and ia32)
echo Output: Installer + Portable versions
echo Machines: FireControl, CutControl
echo.

echo Building for all architectures...
npm run build-universal
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ===================================================================
echo Build completed successfully!
echo ===================================================================
echo.

REM List generated files
if exist "dist" (
    echo Generated files:
    dir "dist" /b
    echo.
    
    REM Calculate total size
    for /f "tokens=3" %%a in ('dir "dist" /-c ^| find "File(s)"') do set totalsize=%%a
    echo Total build size: %totalsize% bytes
    echo.
) else (
    echo WARNING: dist directory not found
)

REM Show installation options
echo ===================================================================
echo Installation Options:
echo ===================================================================
echo.
echo 1. INSTALLER VERSIONS (recommended):
if exist "dist\AIONMECH Pendant Controller-v2.0.0-x64.exe" (
    echo    ✓ 64-bit Installer: dist\AIONMECH Pendant Controller-v2.0.0-x64.exe
) else (
    echo    ❌ 64-bit Installer: Not found
)
if exist "dist\AIONMECH Pendant Controller-v2.0.0-ia32.exe" (
    echo    ✓ 32-bit Installer: dist\AIONMECH Pendant Controller-v2.0.0-ia32.exe
) else (
    echo    ❌ 32-bit Installer: Not found
)
echo.

echo 2. PORTABLE VERSIONS (no installation required):
if exist "dist\AIONMECH Pendant Controller-Portable-v2.0.0-x64.exe" (
    echo    ✓ 64-bit Portable: dist\AIONMECH Pendant Controller-Portable-v2.0.0-x64.exe
) else (
    echo    ❌ 64-bit Portable: Not found
)
if exist "dist\AIONMECH Pendant Controller-Portable-v2.0.0-ia32.exe" (
    echo    ✓ 32-bit Portable: dist\AIONMECH Pendant Controller-Portable-v2.0.0-ia32.exe
) else (
    echo    ❌ 32-bit Portable: Not found
)
echo.

echo ===================================================================
echo Machine Compatibility Matrix:
echo ===================================================================
echo 🔥 PLASMA CUTTING:    CrossFire, CrossFire PRO, CrossFire XR (FireControl)
echo ⚙️ MILLING:           MR-1 (CutControl)
echo � MILLING MACHINE:  MR-1 (CutControl)
echo 🎛️ MANUAL OVERRIDE:   All machines (manual mode)
echo.

echo ===================================================================
echo Windows Compatibility:
echo ===================================================================
echo ✓ Windows 10 (x64, x86)
echo ✓ Windows 11 (x64, x86, ARM64 via emulation)
echo ✓ Any screen resolution (responsive design)
echo ✓ Any DPI scaling (auto-detected)
echo ✓ All hardware configurations
echo.

echo ===================================================================
echo ESP32 Firmware:
echo ===================================================================
echo Upload: esp32_pendant_serial_universal.ino
echo Board: ESP32 Dev Module (ESP32-WROOM)
echo Library: LovyanGFX (install via Arduino Library Manager)
echo Display: GC9A1 240x240 round LCD with Apple Watch glass UI
echo.

echo ===================================================================
echo Usage Instructions:
echo ===================================================================
echo 1. Flash ESP32 with universal firmware
echo 2. Install/run Windows app (choose installer or portable)
echo 3. Connect ESP32 pendant via USB
echo 4. Launch any AIONMECH software (auto-detection)
echo 5. Pendant adapts automatically to detected machine
echo.

echo ===================================================================
echo Quick Test:
echo ===================================================================
echo 1. Connect ESP32 pendant
echo 2. Run portable version to test
echo 3. Open any AIONMECH software
echo 4. Verify machine detection in pendant UI
echo 5. Test jog controls match software
echo.

REM Option to test build
echo.
echo Would you like to test the portable version now? (Y/N)
set /p testbuild="Enter choice: "
if /i "%testbuild%"=="Y" (
    if exist "dist\AIONMECH Pendant Controller-Portable-v2.0.0-x64.exe" (
        echo Launching portable version...
        start "" "dist\AIONMECH Pendant Controller-Portable-v2.0.0-x64.exe"
    ) else if exist "dist\AIONMECH Pendant Controller-Portable-v2.0.0-ia32.exe" (
        echo Launching portable version...
        start "" "dist\AIONMECH Pendant Controller-Portable-v2.0.0-ia32.exe"
    ) else (
        echo No portable version found to test
    )
)

echo.
echo Build process complete!
echo Check the dist\ folder for your universal pendant controller builds.
echo.
pause