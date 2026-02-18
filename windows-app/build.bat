@echo off
echo Installing MR1 Pendant Controller dependencies...
cd /d "%~dp0"

if not exist node_modules (
    echo Installing Node.js dependencies...
    npm install
    if errorlevel 1 (
        echo Failed to install dependencies!
        pause
        exit /b 1
    )
)

echo Building Windows executable...
npm run build-win
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo ✅ Build completed successfully!
echo Executable created in: dist/
echo.
pause
