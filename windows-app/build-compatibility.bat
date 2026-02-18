@echo off
echo Building MR1 Pendant Controller for Windows 10/11 Compatibility...

REM Install dependencies without robotjs first
npm install --ignore-optional

REM Try to install robotjs separately (optional)
echo Installing optional robotjs dependency...
npm install robotjs --optional 2>nul || echo RobotJS install failed - continuing without it

REM Build for x64 (Windows 10/11 64-bit)
echo Building for Windows x64...
npm run build-win-x64

REM Build for ia32 (Windows 10 32-bit, older systems)
echo Building for Windows 32-bit...
npm run build-win-ia32

echo.
echo Build complete! Check the dist folder for:
echo - MR1 Pendant Controller-1.0.0-x64.exe (64-bit portable)
echo - MR1 Pendant Controller-1.0.0-ia32.exe (32-bit portable)
echo - Installer versions in nsis subfolder
echo.
pause