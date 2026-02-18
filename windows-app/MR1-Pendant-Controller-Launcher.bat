@echo off
setlocal
title AIONMECH Pendant Controller Launcher

REM Resolve repo root (this script lives under windows-app)
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%"

REM Preferred packaged EXE path (v2.0.0 folder or latest win-unpacked)
set "DIST_DIR=%SCRIPT_DIR%dist"
set "PKG_DIR=%DIST_DIR%\AIONMECH-Pendant-Controller-v2.0.0"
set "PKG_EXE=%PKG_DIR%\AIONMECH Pendant Controller.exe"
set "UNPACKED_EXE=%DIST_DIR%\win-unpacked\AIONMECH Pendant Controller.exe"
set "MANUAL_EXE=%DIST_DIR%\manual\AIONMECH Pendant Controller-win32-x64\AIONMECH Pendant Controller.exe"

echo ================================================
echo   AIONMECH Pendant Controller - Launcher
echo ================================================
echo.

REM Prefer unpacked build (freshest) before packaged app
if exist "%UNPACKED_EXE%" (
    echo Launching unpacked build...
    start "" "%UNPACKED_EXE%"
    goto :done
)

if exist "%MANUAL_EXE%" (
    echo Launching manual packaged build...
    start "" "%MANUAL_EXE%"
    goto :done
)

if exist "%PKG_EXE%" (
    echo Launching packaged app...
    start "" "%PKG_EXE%"
    goto :done
)

echo Packaged app not found. Attempting dev launch...
REM Dev launch requires Node/Electron in PATH
where npx >nul 2>&1 && (
    echo Starting dev Electron app...
    npx electron "%SCRIPT_DIR%src\main-universal.js" --disable-gpu
) || (
    echo Error: Could not find packaged EXE or npx. Please run npm install inside windows-app.
    echo Expected one of:
    echo   %PKG_EXE%
    echo   %UNPACKED_EXE%
        echo   %MANUAL_EXE%
    pause
    exit /b 1
)

:done
echo.
echo Launcher complete.
popd
endlocal
exit /b 0