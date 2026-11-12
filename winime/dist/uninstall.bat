@echo off
setlocal
cd /d "%~dp0"

fltmc >nul 2>&1
if errorlevel 1 (
    echo Administrator privileges are required. Raising elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

if not exist "%~dp0GikGinIME.dll" (
    echo [ERROR] GikGinIME.dll was not found next to this script.
    pause
    exit /b 1
)

echo Unregistering GikGin input method...
"%SystemRoot%\System32\regsvr32.exe" /u /s "%~dp0GikGinIME.dll"
if errorlevel 1 (
    echo [WARN] Unregistration returned an error. It may already be removed.
)

echo.
echo Done. You can delete this folder.
pause
